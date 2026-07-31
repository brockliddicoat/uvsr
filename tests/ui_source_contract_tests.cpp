#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
        std::string source = contents.str();
        source.erase(
            std::remove(source.begin(), source.end(), '\r'),
            source.end());
        return source;
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

    struct ParsedCommandCatalogEntry
    {
        std::string name;
        std::string section;
        bool action = false;
    };

    std::vector<ParsedCommandCatalogEntry> ParseCommandCatalog(
        std::string_view catalog)
    {
        constexpr std::string_view ValueAnchor =
            "MakeUiSettingsValueCommand(";
        constexpr std::string_view ActionAnchor =
            "MakeUiSettingsActionCommand(";
        constexpr std::string_view SectionAnchor =
            "UiSettingsCommandSection::";

        std::vector<ParsedCommandCatalogEntry> entries;
        size_t cursor = 0u;
        while (cursor < catalog.size())
        {
            const size_t valuePosition = catalog.find(ValueAnchor, cursor);
            const size_t actionPosition = catalog.find(ActionAnchor, cursor);
            if (valuePosition == std::string_view::npos &&
                actionPosition == std::string_view::npos)
            {
                break;
            }

            const bool action =
                valuePosition == std::string_view::npos ||
                (actionPosition != std::string_view::npos &&
                    actionPosition < valuePosition);
            const size_t entryPosition =
                action ? actionPosition : valuePosition;
            const size_t nextValue = catalog.find(
                ValueAnchor, entryPosition + 1u);
            const size_t nextAction = catalog.find(
                ActionAnchor, entryPosition + 1u);
            size_t nextEntry = catalog.size();
            if (nextValue != std::string_view::npos)
                nextEntry = std::min(nextEntry, nextValue);
            if (nextAction != std::string_view::npos)
                nextEntry = std::min(nextEntry, nextAction);

            const std::string_view entry =
                catalog.substr(entryPosition, nextEntry - entryPosition);
            const size_t nameBegin = entry.find('"');
            const size_t nameEnd = nameBegin == std::string_view::npos
                ? std::string_view::npos
                : entry.find('"', nameBegin + 1u);
            const size_t sectionBegin = entry.find(SectionAnchor);
            if (nameBegin == std::string_view::npos ||
                nameEnd == std::string_view::npos ||
                sectionBegin == std::string_view::npos)
            {
                Fail("Settings command catalog entry is malformed.");
                cursor = nextEntry;
                continue;
            }

            const size_t sectionNameBegin =
                sectionBegin + SectionAnchor.size();
            size_t sectionNameEnd = sectionNameBegin;
            while (sectionNameEnd < entry.size())
            {
                const unsigned char character =
                    static_cast<unsigned char>(entry[sectionNameEnd]);
                if (!std::isalnum(character) && character != '_')
                    break;
                ++sectionNameEnd;
            }
            entries.push_back({
                std::string(entry.substr(
                    nameBegin + 1u, nameEnd - nameBegin - 1u)),
                std::string(entry.substr(
                    sectionNameBegin,
                    sectionNameEnd - sectionNameBegin)),
                action
            });
            cursor = nextEntry;
        }
        return entries;
    }

    bool ContainsQuotedLiteral(
        std::string_view source,
        std::string_view value)
    {
        const std::string literal = "\"" + std::string(value) + "\"";
        return source.find(literal) != std::string_view::npos;
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
    const std::string_view keyboardUpdate = ExtractSection(
        source,
        "virtual bool KeyboardUpdate(",
        "virtual void buildUI(void) override",
        "UI keyboard routing");
    ExpectContains(
        keyboardUpdate,
        "const bool captured = ImGui_Renderer::KeyboardUpdate(",
        "base ImGui keyboard routing");
    ExpectContains(
        keyboardUpdate,
        "key == GLFW_KEY_F",
        "flashlight keyboard shortcut");
    ExpectContains(
        keyboardUpdate,
        "action == GLFW_PRESS",
        "single-press flashlight shortcut");
    ExpectContains(
        keyboardUpdate,
        "!captured",
        "captured-key flashlight isolation");
    ExpectContains(
        keyboardUpdate,
        "!ImGui::GetIO().WantTextInput",
        "text-input flashlight isolation");
    ExpectContains(
        keyboardUpdate,
        "m_app->ToggleFlashlight();",
        "flashlight shortcut dispatch");
    ExpectContains(
        source,
        "Flashlight unavailable while PBR rendering is disabled",
        "legacy-lighting flashlight shortcut rejection");
    ExpectOrdered(
        keyboardUpdate,
        "const bool captured = ImGui_Renderer::KeyboardUpdate(",
        "key == GLFW_KEY_F",
        "base ImGui handling before flashlight shortcut");
    const std::string_view rendererReset = ExtractSection(
        source,
        "void ResetAllRendererSettings()",
        "void SynchronizeCameraInput()",
        "renderer reset");
    ExpectContains(
        rendererReset,
        "m_ui.FlashlightEnabled = DefaultFlashlightEnabled;",
        "flashlight reset default");
    ExpectContains(
        rendererReset,
        "m_FlashlightTransition = 0.f;",
        "flashlight reset endpoint");
    ExpectContains(
        rendererReset,
        "m_ui.Flashlight = DefaultFlashlightSettings;",
        "flashlight setting reset defaults");
    ExpectContains(
        rendererReset,
        "m_FlashlightHotspot->intensity = 0.f;",
        "flashlight hotspot reset endpoint");
    ExpectContains(
        rendererReset,
        "ResetFlashlightMotion();",
        "flashlight motion reset");
    ExpectContains(
        rendererReset,
        "m_ui.EnableAmbientFill = true;",
        "ambient fill reset default");
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
        "UI reference version: `2026-07-31.5`.",
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
    ExpectContains(
        uiReferenceSource,
        "SVSM Developer Options use a permanently fixed requested-settings "
        "schema.",
        "fixed editable SVSM requested-settings guidance");
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
    const std::filesystem::path imguiRuntimePolicyPath =
        std::filesystem::path(argv[1]) /
        "overrides" /
        "imgui-runtime-policy.patch";
    const std::string imguiRuntimePolicy =
        ReadFile(imguiRuntimePolicyPath);
    const std::string uiSkinSource = ReadFile(
        std::filesystem::path(argv[1]) / "src" / "ui_skin.h");
    const std::string uiCommandsSource = ReadFile(
        std::filesystem::path(argv[1]) / "src" / "ui_commands.h");
    const std::string uiCommandLayoutSource = ReadFile(
        std::filesystem::path(argv[1]) / "src" / "ui_command_layout.h");
    const std::string uiSettingsCommandCatalogSource = ReadFile(
        std::filesystem::path(argv[1]) /
        "src" /
        "ui_settings_command_catalog.h");
    if (imguiRuntimePolicy.empty() ||
        uiSkinSource.empty() ||
        uiCommandsSource.empty() ||
        uiCommandLayoutSource.empty() ||
        uiSettingsCommandCatalogSource.empty())
    {
        std::cerr
            << "FAIL: could not read the UI skin/command runtime policy "
               "or command-layout helper\n";
        return 2;
    }
    ExpectContains(
        cmakeSource,
        "overrides/imgui-dropdown-roll.patch",
        "ordered ImGui dropdown-roll staging");
    ExpectOrdered(
        cmakeSource,
        "overrides/imgui-dropdown-roll.patch",
        "overrides/imgui-runtime-policy.patch",
        "ordered ImGui runtime-policy staging");
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
    if (CountOccurrences(cmakeSource, "src/app/DeviceManager.cpp") < 2)
    {
        Fail("Donut fullscreen-shortcut override must be staged and replace "
            "the pristine target source.");
    }
    ExpectContains(
        cmakeSource,
        "include/donut/app/DeviceManager.h",
        "Donut fullscreen-shortcut interface override staging");
    ExpectContains(
        donutAppOverride,
        "+        std::atomic_bool m_SceneLoaded;",
        "thread-safe Donut scene-loaded override");
    ExpectContains(
        donutAppOverride,
        "+        virtual bool ShouldSuppressFullscreenShortcut() const",
        "Donut render-pass fullscreen-shortcut suppression hook");
    const std::string_view deviceManagerOverride = ExtractSection(
        donutAppOverride,
        "diff --git a/src/app/DeviceManager.cpp",
        "diff --git a/include/donut/app/UserInterfaceUtils.h",
        "Donut fullscreen-shortcut override");
    ExpectOrdered(
        deviceManagerOverride,
        "ShouldSuppressFullscreenShortcut()",
        "ToggleFullscreen();",
        "fullscreen suppression before fullscreen mutation");
    ExpectContains(
        donutAppOverride,
        "+        bool showMaterialDomain = true);",
        "Material Editor visibility override contract");
    ExpectContains(
        donutAppOverride,
        "+        int domainIndex = int(material->domain);",
        "Material Editor domain storage safety");
    ExpectContains(
        uiSkinSource,
        "enum class UiSkin\n"
        "    {\n"
        "        Amp,\n"
        "        Og,\n"
        "        Count\n"
        "    };",
        "exact Amp and OG skin model");
    ExpectContains(
        uiSkinSource,
        "UiSkin::Amp,\n"
        "        UiSkin::Og",
        "stable Amp and OG picker order");
    ExpectContains(
        uiSkinSource,
        "case UiSkin::Amp:",
        "Amp behavior profile");
    ExpectContains(
        uiSkinSource,
        "return { true, false, true, true };",
        "authored animated Amp behavior");
    ExpectContains(
        uiSkinSource,
        "case UiSkin::Og:",
        "OG behavior profile");
    ExpectContains(
        uiSkinSource,
        "return { false, true, false, false };",
        "motion-free stock OG behavior");
    ExpectContains(
        uiSkinSource,
        "normalized == \"amp\"",
        "Amp command alias");
    ExpectContains(
        uiSkinSource,
        "normalized == \"og\"",
        "OG command alias");
    for (const std::string_view retiredSkinContract : {
            std::string_view("UiSkin::Current"),
            std::string_view("UiSkin::ImGuiClassic"),
            std::string_view("UiSkin::ChatGptCodex"),
            std::string_view("UiSkin::UnrealEngine5"),
            std::string_view("UiSkin::RenderLab"),
            std::string_view("normalized == \"current\""),
            std::string_view("normalized == \"original\""),
            std::string_view("normalized == \"chatgpt-codex\""),
            std::string_view("normalized == \"ue5\""),
            std::string_view("normalized == \"signal\"") })
    {
        ExpectAbsent(
            uiSkinSource,
            retiredSkinContract,
            "retired UI skin implementation and aliases");
    }
    ExpectContains(
        uiCommandsSource,
        "ParseUiCommand(",
        "slash-command parser");
    ExpectContains(
        imguiRuntimePolicy,
        "SetUvsrUiBehavior",
        "runtime ImGui behavior selector");
    ExpectContains(
        imguiRuntimePolicy,
        "UvsrStockWidgetRendering",
        "runtime stock-widget branch");
    ExpectContains(
        imguiRuntimePolicy,
        "if (!g.UvsrUiMotionEnabled)",
        "motion-free ImGui endpoint branch");
    ExpectContains(
        imguiRuntimePolicy,
        "if (g.UvsrStockWidgetRendering ||",
        "stock ImGui disabled-color branch");
    ExpectContains(
        imguiRuntimePolicy,
        "draw_list->AddTriangleFilled(",
        "stock ImGui arrow primitive");
    ExpectContains(
        imguiRuntimePolicy,
        "const float highlight_amount = g.UvsrStockWidgetRendering",
        "stock ImGui combo rendering");
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
    const std::filesystem::path pixelZoomShaderPath =
        std::filesystem::path(argv[1]) /
        "src" /
        "pixel_zoom_ps.hlsl";
    const std::string pixelZoomShader =
        ReadFile(pixelZoomShaderPath);
    if (pixelZoomShader.empty())
    {
        std::cerr << "FAIL: could not read "
                  << pixelZoomShaderPath << '\n';
        return 2;
    }
    const std::filesystem::path temporalAaPath =
        std::filesystem::path(argv[1]) /
        "src" /
        "temporal_aa.cpp";
    const std::string temporalAaSource = ReadFile(temporalAaPath);
    const std::filesystem::path sharpenShaderPath =
        std::filesystem::path(argv[1]) /
        "src" /
        "temporal_aa_sharpen_cs.hlsl";
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
    ExpectContains(
        source,
        "ImGui::TextDisabled(\"Factory Shader Topology Locked\")",
        "factory experiment topology notice");
    ExpectContains(
        source,
        "This experiment build renders only the settings selected by a ",
        "factory experiment topology notice");
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
                "if (lightsOpen)"},
            {
                "const bool shadowsOpen = DrawCollapsingHeader(",
                "if (shadowsOpen)"} })
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
        "UiSkin                              Skin = DefaultUiSkin",
        "session-only UI skin state");
    ExpectContains(
        source,
        "m_ComposedUiSkin = m_ui.Skin;",
        "immutable composed-skin snapshot");
    ExpectContains(
        source,
        "ApplyUiSkin(m_ComposedUiSkin, m_UiDisplayScale)",
        "per-frame composed UI skin application");
    ExpectContains(
        source,
        "GetUiSkinBehavior(m_ComposedUiSkin).backdropEnabled",
        "same-frame composed skin backdrop policy");
    ExpectContains(
        source,
        "else if (!uiMotionEnabled)",
        "motion-free pixel zoom endpoint");
    ExpectContains(
        source,
        "skin == UiSkin::Og ? UiSkin::Og : UiSkin::Amp",
        "two-skin runtime fallback");
    ExpectContains(
        source,
        "ImGui::StyleColorsDark(&style);",
        "stock OG ImGui style base");
    ExpectContains(
        source,
        "if (resolvedSkin == UiSkin::Og)",
        "stock OG visual-token branch");
    ExpectContains(
        source,
        "if (resolvedSkin == UiSkin::Og)\n"
        "        {\n"
        "            style.ScrollbarRounding = 0.f;",
        "square OG scrollbar");
    ExpectContains(
        source,
        "tokens.drawControlOutlines = false;",
        "stock OG authored-outline suppression");
    ExpectContains(
        source,
        "tokens.drawScrollEdgeFades = false;",
        "stock OG scroll-decoration suppression");
    ExpectContains(
        source,
        "if (resolvedSkin == UiSkin::Amp)",
        "authored Amp metric branch");
    ExpectContains(
        source,
        "style.ScrollbarRounding = 8.f;",
        "rounded Amp scrollbar");
    ExpectContains(
        source,
        "ImGui::IsUvsrStockWidgetRenderingEnabled()",
        "stock OG default-font selection");
    ExpectContains(
        source,
        "GetUiSkinBehavior(m_ComposedUiSkin).expandedWordSpacing",
        "skin-specific word-spacing behavior");
    ExpectContains(
        source,
        "UiSkinLabel(m_ui.Skin).data()",
        "General UI skin picker");
    const std::string_view uiSkinPicker = ExtractSection(
        source,
        "ImGui::TextUnformatted(\"Interface Skin\")",
        "ImGui::TextUnformatted(\"Graphics Adapter\")",
        "General Interface Skin picker");
    ExpectContains(
        uiSkinPicker,
        "DrawPresetResetIcon(\n"
        "                    \"Interface Skin\"",
        "Interface Skin reset identity");
    ExpectContains(
        uiSkinPicker,
        "QueueDeferredControlUiAction(",
        "frame-end UI skin reset");
    ExpectContains(
        uiSkinPicker,
        "UiSkinValues",
        "two-skin General picker source");
    ExpectAbsent(
        uiSkinPicker,
        "\"UI Skin\"",
        "retired General UI Skin label");
    ExpectAbsent(
        uiSkinPicker,
        "Original ImGui",
        "retired Original skin wording");
    ExpectContains(
        source,
        "Use /skin [amp|og]",
        "exact Amp and OG slash-command help");

    const std::string_view commandCatalog = ExtractSection(
        uiSettingsCommandCatalogSource,
        "UiSettingsCommandCatalog = {{",
        "        }};",
        "Settings command catalog");
    const std::vector<ParsedCommandCatalogEntry> commandCatalogEntries =
        ParseCommandCatalog(commandCatalog);
    std::set<std::string> commandNames;
    std::set<std::string> valueCommandNames;
    std::set<std::string> actionCommandNames;
    for (const ParsedCommandCatalogEntry& entry : commandCatalogEntries)
    {
        if (!commandNames.insert(entry.name).second)
        {
            Fail("Settings command catalog path '" + entry.name +
                "' must be unique.");
        }
        (entry.action ? actionCommandNames : valueCommandNames).insert(
            entry.name);
    }
    if (commandCatalogEntries.size() != 245u ||
        commandNames.size() != 245u)
    {
        Fail("Settings command catalog must contain exactly 245 unique "
            "entries.");
    }
    if (valueCommandNames.size() != 235u)
    {
        Fail("Settings command catalog must contain exactly 235 value "
            "entries.");
    }
    const std::set<std::string> expectedActionCommandNames = {
        "open-scene-folder",
        "visibility-benchmark",
        "aa-motion-test",
        "cancel-benchmark",
        "svsm-camera-motion-test",
        "svsm-sun-motion-test",
        "cancel-svsm-motion-test",
        "reset-settings",
        "screenshot",
        "restart"
    };
    if (actionCommandNames != expectedActionCommandNames)
    {
        Fail("Settings command catalog must contain the exact ten supported "
            "actions.");
    }

    const std::string compactCommandCatalog =
        RemoveAsciiWhitespace(uiSettingsCommandCatalogSource);
    ExpectContains(
        compactCommandCatalog,
        "std::array<UiSettingsCommandDefinition,245>"
        "UiSettingsCommandCatalog",
        "exact Settings command catalog size");
    ExpectContains(
        uiSettingsCommandCatalogSource,
        "UiSettingsFactoryMutationPolicy factoryMutationPolicy",
        "typed factory-mutation policy");
    ExpectAbsent(
        uiSettingsCommandCatalogSource,
        "bool factoryMutationPolicy",
        "raw boolean factory-mutation policy");

    const std::string_view commandRegistry = ExtractSection(
        source,
        "    struct UiSettingsCommandBinding",
        "    static std::string NormalizeCommandAscii(",
        "catalog-derived Settings command registry");
    const std::string compactCommandRegistry =
        RemoveAsciiWhitespace(commandRegistry);
    ExpectContains(
        compactCommandRegistry,
        "std::array<UiSettingsCommandBinding,"
        "UiSettingsCommandCatalog.size()>bindings{};",
        "catalog-sized Settings command registry");
    ExpectContains(
        compactCommandRegistry,
        "bindings[index]={"
        "UiSettingsCommandCatalog[index].name,"
        "UiSettingsCommandCatalog[index].section,"
        "UiSettingsCommandCatalog[index].kind};",
        "catalog-derived Settings command bindings");
    ExpectContains(
        compactCommandRegistry,
        "static_assert(UiSettingsCommandBindings.size()=="
        "UiSettingsCommandCatalog.size());",
        "registry and catalog size equivalence");
    ExpectContains(
        compactCommandRegistry,
        "static_assert(UiSettingsCommandBindings.size()==245u);",
        "exact Settings command registry size");
    ExpectAbsent(
        compactCommandRegistry,
        "std::array<UiSettingsCommandBinding,17>",
        "retired short Settings command registry");
    if (CountOccurrences(commandRegistry, "\"") != 0u)
    {
        Fail("Settings command registry must derive every path from the "
            "catalog instead of repeating path literals.");
    }

    struct ValueDispatchContract
    {
        std::string_view section;
        std::string_view function;
        std::string_view implementation;
    };
    const std::vector<ValueDispatchContract> valueDispatchers = {
        {
            "Ui",
            "DispatchUiCommandValue",
            ExtractSection(
                source,
                "    bool DispatchUiCommandValue(",
                "    bool DispatchGeneralCommandValue(",
                "UI value-command dispatcher")
        },
        {
            "General",
            "DispatchGeneralCommandValue",
            ExtractSection(
                source,
                "    bool DispatchGeneralCommandValue(",
                "    bool DispatchVisibilityCommandValue(",
                "General value-command dispatcher")
        },
        {
            "Visibility",
            "DispatchVisibilityCommandValue",
            ExtractSection(
                source,
                "    bool DispatchVisibilityCommandValue(",
                "    bool DispatchBufferCommandValue(",
                "Visibility value-command dispatcher")
        },
        {
            "Buffers",
            "DispatchBufferCommandValue",
            ExtractSection(
                source,
                "    bool DispatchBufferCommandValue(",
                "    bool DispatchStatisticsCommandValue(",
                "Buffer value-command dispatcher")
        },
        {
            "Statistics",
            "DispatchStatisticsCommandValue",
            ExtractSection(
                source,
                "    bool DispatchStatisticsCommandValue(",
                "    bool DispatchAliasingCommandValue(",
                "Statistics value-command dispatcher")
        },
        {
            "Aliasing",
            "DispatchAliasingCommandValue",
            ExtractSection(
                source,
                "    bool DispatchAliasingCommandValue(",
                "    bool DispatchSkyCommandValue(",
                "Aliasing value-command dispatcher")
        },
        {
            "Sky",
            "DispatchSkyCommandValue",
            ExtractSection(
                source,
                "    bool DispatchSkyCommandValue(",
                "    bool DispatchLightCommandValue(",
                "Sky value-command dispatcher")
        },
        {
            "Lights",
            "DispatchLightCommandValue",
            ExtractSection(
                source,
                "    struct FlashlightFloatCommandBinding",
                "    bool DispatchScreenSpaceShadowCommandValue(",
                "Light value-command dispatcher")
        },
        {
            "ScreenSpaceDirectionalShadows",
            "DispatchScreenSpaceShadowCommandValue",
            ExtractSection(
                source,
                "    bool DispatchScreenSpaceShadowCommandValue(",
                "    struct SvsmBoolCommandBinding",
                "screen-space directional-shadow value-command dispatcher")
        },
        {
            "SparseVirtualShadowMaps",
            "DispatchSvsmCommandValue",
            ExtractSection(
                source,
                "    struct SvsmBoolCommandBinding",
                "    struct CsmBoolCommandBinding",
                "SVSM value-command dispatcher")
        },
        {
            "DiagnosticCascadedShadowMaps",
            "DispatchCsmCommandValue",
            ExtractSection(
                source,
                "    struct CsmBoolCommandBinding",
                "    bool DispatchMaterialCommandValue(",
                "diagnostic CSM value-command dispatcher")
        },
        {
            "Materials",
            "DispatchMaterialCommandValue",
            ExtractSection(
                source,
                "    bool DispatchMaterialCommandValue(",
                "    bool DispatchCommandValue(",
                "Material value-command dispatcher")
        }
    };
    const std::string_view commandValueRouter = ExtractSection(
        source,
        "    bool DispatchCommandValue(",
        "    bool DispatchCommandAction(",
        "Settings value-command section router");
    const std::string compactCommandValueRouter =
        RemoveAsciiWhitespace(commandValueRouter);
    for (const ValueDispatchContract& dispatcher : valueDispatchers)
    {
        ExpectContains(
            dispatcher.implementation,
            "const std::string_view path = definition.name;",
            "definition-owned value-command dispatch");
        const std::string route =
            "caseUiSettingsCommandSection::" +
            std::string(dispatcher.section) +
            ":return" +
            std::string(dispatcher.function) +
            "(";
        ExpectContains(
            compactCommandValueRouter,
            route,
            "complete Settings value-command section routing");
    }
    ExpectContains(
        compactCommandValueRouter,
        "caseUiSettingsCommandSection::Footer:"
        "caseUiSettingsCommandSection::Count:break;",
        "non-value Settings command section routing");

    ExpectContains(
        source,
        "error = \"No change: \" + std::string(path) +\n"
        "            \" already has the requested value.\";",
        "stable unchanged-mutation failure");
    if (CountOccurrences(
            source,
            "RejectUnchangedCommandMutation(path, error)") < 6u)
    {
        Fail("Every generic command value helper must reject unchanged "
            "mutations before assignment.");
    }

    const std::string_view screenSpaceShadowCommand =
        valueDispatchers[8].implementation;
    const std::string_view svsmCommand =
        valueDispatchers[9].implementation;
    const std::string_view csmCommand =
        valueDispatchers[10].implementation;
    for (const std::string_view shadowCommand :
        { screenSpaceShadowCommand, svsmCommand, csmCommand })
    {
        ExpectOrdered(
            shadowCommand,
            "operation != CommandValueOperation::Get",
            "m_app->HasPrimaryDirectionalLight()",
            "shadow command availability before mutation");
        ExpectContains(
            shadowCommand,
            "require deferred PBR and a",
            "visible shadow-section availability error");
        ExpectContains(
            shadowCommand,
            "primary ",
            "visible shadow-section primary-light requirement");
        ExpectContains(
            shadowCommand,
            "directional light.",
            "visible shadow-section directional-light requirement");
    }
    ExpectContains(
        screenSpaceShadowCommand,
        "const ScreenSpaceDirectionalShadowSettings factoryDefaults;",
        "screen-space directional-shadow per-control factory resets");
    ExpectContains(
        csmCommand,
        "const DiagnosticCascadedShadowMapSettings factoryDefaults;",
        "CSM per-control factory resets");
    for (const std::string_view csmAvailabilityPath : {
            std::string_view("shadows.csm.filter-taps"),
            std::string_view("shadows.csm.filter-radius"),
            std::string_view("shadows.csm.dirty-rectangles"),
            std::string_view("shadows.csm.minimum-scroll-overlap"),
            std::string_view(
                "shadows.csm.accurate-caster-hull-culling"),
            std::string_view(
                "shadows.csm.conservative-saturated-slope-shortcut"),
            std::string_view(
                "shadows.csm.precomputed-receiver-hull-axes") })
    {
        if (!ContainsQuotedLiteral(csmCommand, csmAvailabilityPath))
        {
            Fail("Diagnostic CSM command availability is missing for '" +
                std::string(csmAvailabilityPath) + "'.");
        }
    }
    ExpectContains(
        valueDispatchers[2].implementation,
        "operation == CommandValueOperation::Get &&",
        "Visibility custom-profile mutation reapplies its compound preset");
    ExpectContains(
        valueDispatchers[5].implementation,
        "TemporalAaSampleResurrectionOverride>, 4>",
        "sample resurrection command maps every stored state");
    ExpectContains(
        valueDispatchers[5].implementation,
        "\"preset\",\n"
        "                            TemporalAaSampleResurrectionOverride::FromPreset",
        "sample resurrection get and reset preserve the preset default");
    ExpectContains(
        valueDispatchers[2].implementation,
        "candidate.quality ==\n"
        "                        ScreenSpaceVisibilityQuality::Custom",
        "Visibility custom-profile Get reports its preset origin");
    ExpectContains(
        svsmCommand,
        "std::pair<std::string_view, SvsmDebugView>, 12>",
        "complete SVSM debug-view command map");
    for (const std::string_view debugView : {
            std::string_view("required-pages"),
            std::string_view("resident-pages"),
            std::string_view("cached-pages"),
            std::string_view("dirty-pages"),
            std::string_view("rendered-pages") })
    {
        if (!ContainsQuotedLiteral(svsmCommand, debugView))
        {
            Fail("SVSM debug-view command is missing '" +
                std::string(debugView) + "'.");
        }
    }
    ExpectContains(
        svsmCommand,
        "const uint32_t resetBudget = std::min(\n"
        "                    256u,\n"
        "                    candidate.physicalPageCount);",
        "finite SVSM page-budget reset");
    ExpectContains(
        svsmCommand,
        "candidate.pageRenderBudget = resetBudget;",
        "SVSM page-budget reset commit");
    ExpectContains(
        svsmCommand,
        "m_CommandRememberedSvsmPageBudget =\n"
        "            rememberedPageBudget;",
        "transactional remembered SVSM page-budget commit");

    const std::string_view commandActionDispatcher = ExtractSection(
        source,
        "    bool DispatchCommandAction(",
        "    void AppendDynamicCommandValues(",
        "Settings action dispatcher");
    size_t coveredCommandCount = 0u;
    for (const ParsedCommandCatalogEntry& entry : commandCatalogEntries)
    {
        std::string_view implementation = commandActionDispatcher;
        if (!entry.action)
        {
            implementation = {};
            for (const ValueDispatchContract& dispatcher :
                valueDispatchers)
            {
                if (dispatcher.section == std::string_view(entry.section))
                {
                    implementation = dispatcher.implementation;
                    break;
                }
            }
        }
        if (implementation.empty())
        {
            Fail("Settings command '" + entry.name +
                "' has no section dispatcher.");
            continue;
        }
        if (!ContainsQuotedLiteral(implementation, entry.name))
        {
            Fail("Settings command '" + entry.name +
                "' is missing from its section dispatcher.");
            continue;
        }
        ++coveredCommandCount;
    }
    if (coveredCommandCount != 245u)
    {
        Fail("Section dispatchers must cover all 245 catalog commands "
            "literally.");
    }
    for (const std::string& action : expectedActionCommandNames)
    {
        ExpectContains(
            commandActionDispatcher,
            "action == \"" + action + "\"",
            "exact Settings action dispatch");
    }

    const std::string_view mutationPolicy = ExtractSection(
        source,
        "    bool CheckCommandMutationAllowed(",
        "    static bool ApplyCommandBool(",
        "unified Settings command mutation policy");
    ExpectContains(
        mutationPolicy,
        "UiSettingsFactoryMutationPolicy::UiSafe",
        "factory-shader command mutation policy");
    ExpectContains(
        mutationPolicy,
        "IsCommandRuntimeMutationLocked(definition)",
        "runtime command mutation policy");
    ExpectAbsent(
        source,
        "IsCommandMutationLocked(",
        "retired raw command-mutation bypass");
    if (CountOccurrences(source, "CheckCommandMutationAllowed(") != 4u)
    {
        Fail("Every mutating command route must share the one catalog-aware "
            "policy gate.");
    }

    const std::string_view executeCommand = ExtractSection(
        source,
        "    void ExecuteUiCommand(",
        "    void CompleteCommandInput(",
        "Settings command execution");
    const std::string_view runCommand = ExtractSection(
        executeCommand,
        "        if (command.verb == UiCommandVerb::Run)\n"
        "        {",
        "        if (command.verb == UiCommandVerb::Reset &&",
        "catalog action execution");
    ExpectOrdered(
        runCommand,
        "FindSettingsCommandDefinition(command.action)",
        "definition->Supports(UiSettingsCommandVerb::Run)",
        "catalog action verb validation");
    ExpectOrdered(
        runCommand,
        "definition->Supports(UiSettingsCommandVerb::Run)",
        "CheckCommandMutationAllowed(*definition, error)",
        "catalog action mutation validation");
    ExpectOrdered(
        runCommand,
        "CheckCommandMutationAllowed(*definition, error)",
        "DispatchCommandAction(",
        "validated catalog action dispatch");
    const std::string_view valueCommand = ExtractSection(
        executeCommand,
        "        const UiSettingsCommandDefinition* definition =\n"
        "            FindSettingsCommandDefinition(command.path);",
        "        SetCommandResult(\n"
        "            std::string(definition->name) + \" = \" + value);",
        "catalog value-command execution");
    ExpectOrdered(
        valueCommand,
        "FindSettingsCommandDefinition(command.path)",
        "GetSettingsCommandVerb(operation)",
        "catalog value-command lookup");
    ExpectOrdered(
        valueCommand,
        "GetSettingsCommandVerb(operation)",
        "definition->Supports(settingsVerb)",
        "catalog value-command verb validation");
    ExpectOrdered(
        valueCommand,
        "definition->Supports(settingsVerb)",
        "CheckCommandMutationAllowed(*definition, error)",
        "catalog value-command mutation validation");
    ExpectOrdered(
        valueCommand,
        "CheckCommandMutationAllowed(*definition, error)",
        "DispatchCommandValue(",
        "validated catalog value-command dispatch");

    const std::string_view commandCompletion = ExtractSection(
        source,
        "    std::vector<std::string> GetCommandCompletionCandidates(",
        "    void ExecuteUiCommand(",
        "catalog-driven command completion");
    if (CountOccurrences(
            commandCompletion, "UiSettingsCommandBindings") < 2u)
    {
        Fail("Path and action completion must derive from the complete "
            "Settings command binding registry.");
    }
    ExpectContains(
        commandCompletion,
        "FindSettingsCommandDefinition(completion.valuePath)",
        "path-scoped value completion");
    ExpectContains(
        commandCompletion,
        "std::string_view remaining = definition->domain;",
        "selected-path catalog-domain completion");
    ExpectContains(
        commandCompletion,
        "AppendDynamicCommandValues(",
        "selected-path runtime value completion");
    ExpectAbsent(
        commandCompletion,
        "for (const UiSettingsCommandDefinition& definition :\n"
        "                UiSettingsCommandCatalog)",
        "global cross-path value completion pooling");
    const std::string_view listCommand = ExtractSection(
        executeCommand,
        "        if (command.verb == UiCommandVerb::List)",
        "        if (command.verb == UiCommandVerb::Run &&",
        "catalog-driven Settings listing");
    ExpectContains(
        listCommand,
        "for (const UiSettingsCommandDefinition& definition :\n"
        "                UiSettingsCommandCatalog)",
        "complete Settings catalog listing");
    ExpectContains(
        listCommand,
        "GetSettingsCommandVerbList(definition)",
        "catalog verb listing");
    ExpectContains(
        listCommand,
        "AppendDynamicCommandValues(",
        "runtime value listing");

    const std::string_view dynamicCommandValues = ExtractSection(
        source,
        "    void AppendDynamicCommandValues(",
        "    std::vector<std::string> GetCommandCompletionCandidates(",
        "dynamic Settings command discovery");
    for (const std::string_view dynamicPath : {
            std::string_view("gpu.adapter"),
            std::string_view("scene.current"),
            std::string_view("sky.environment"),
            std::string_view("light.selected"),
            std::string_view("material.selected") })
    {
        ExpectContains(
            dynamicCommandValues,
            "path == \"" + std::string(dynamicPath) + "\"",
            "dynamic Settings path discovery");
    }
    for (const std::string_view discoverySource : {
            std::string_view("m_ui.GpuAdapterChoices"),
            std::string_view("m_app->GetAvailableScenes()"),
            std::string_view("ImageBasedLightingSource::Count"),
            std::string_view("GetImageBasedLightingSourceInfo("),
            std::string_view("m_app->GetEditableLights()"),
            std::string_view("GetSceneGraph()->GetMaterials()") })
    {
        ExpectContains(
            dynamicCommandValues,
            discoverySource,
            "live adapter, scene, environment, light, and material discovery");
    }
    ExpectContains(
        source,
        "Enter applies | Tab completes | Up/Down history | / closes | Esc cancels edit",
        "font-safe slash-command shortcut legend");
    ExpectContains(
        source,
        "m_ui.ShowUI = !m_ui.ShowUI",
        "Escape Settings toggle");
    ExpectContains(
        source,
        "m_SettingsAppearance = uiMotionEnabled",
        "Settings open-and-close appearance target");
    ExpectContains(
        source,
        ": m_ui.ShowUI ? 1.f : 0.f",
        "motion-free Settings appearance endpoint");
    ExpectContains(
        source,
        "settingsWindowFlags |= ImGuiWindowFlags_NoInputs",
        "noninteractive Settings close animation");
    ExpectContains(
        source,
        "TryApplyDeferredDropdownUiActions(",
        "frame-end deferred settings mutation");
    ExpectContains(
        source,
        "!uiMotionEnabled",
        "motion-free immediate deferred-action policy");
    ExpectContains(
        source,
        "for (ImDrawCmd& command : drawList->CmdBuffer)",
        "Settings appearance clip-rectangle transform");
    ExpectAbsent(
        source,
        "m_SettingsEntranceStarted",
        "removed automatic Settings entrance gate");

    const std::string_view commandInterface = ExtractSection(
        source,
        "    void DrawCommandInterface()",
        "    void DrawMaterialInspector(",
        "command-interface layout integration");
    ExpectContains(
        commandInterface,
        "const CommandInterfaceLayout& commandLayout =\n"
        "            m_CommandLayout;",
        "frame-resolved command layout");
    ExpectOrdered(
        commandInterface,
        "if (!commandLayout.fits)",
        "ImGui::Begin(",
        "unrenderable command-layout withholding");
    const std::string_view withheldCommandLayout = ExtractSection(
        commandInterface,
        "        if (!commandLayout.fits)",
        "        ImGui::SetNextWindowPos(",
        "unrenderable command-layout focus retention");
    ExpectOrdered(
        withheldCommandLayout,
        "if (m_CommandOpen)",
        "m_CommandFocusRequested = true",
        "open command focus re-arm while layout is withheld");
    ExpectContains(
        commandInterface,
        "commandLayout.bottom",
        "bottom-aligned command position");
    ExpectContains(
        commandInterface,
        "ImVec2(0.f, 1.f)",
        "bottom command-window pivot");
    ExpectContains(
        commandInterface,
        "ImGui::SetNextWindowSize(",
        "fixed command lane size");
    ExpectContains(
        commandInterface,
        "commandLayout.width,\n"
        "                commandLayout.height",
        "full reserved command geometry");
    ExpectAbsent(
        commandInterface,
        "ImGuiWindowFlags_AlwaysAutoResize",
        "same-frame fixed command lane");
    ExpectOrdered(
        commandInterface,
        "m_CommandAppearance = commandMotionEnabled",
        "if (!m_CommandOpen && m_CommandAppearance <= 0.f)",
        "command appearance advances before its hidden endpoint");
    ExpectOrdered(
        commandInterface,
        "if (!m_CommandOpen && m_CommandAppearance <= 0.f)",
        "if (!commandLayout.fits)",
        "command appearance advances before layout withholding");
    ExpectContains(
        commandInterface,
        "SmoothPixelZoomVisibility(m_CommandAppearance)",
        "Amp command fade curve");
    ExpectContains(
        commandInterface,
        "PixelZoomMinimumWindowScale +",
        "Amp command zoom curve");
    ExpectContains(
        commandInterface,
        ": m_CommandOpen ? 1.f : 0.f",
        "motion-free OG command endpoints");
    ExpectContains(
        commandInterface,
        "commandWindowFlags |= ImGuiWindowFlags_NoInputs",
        "noninteractive command close animation");
    ExpectContains(
        commandInterface,
        "commandWindowFlags |= ImGuiWindowFlags_NoMouseInputs",
        "mouse-safe command open animation");
    ExpectOrdered(
        commandInterface,
        "ImGui::End();",
        "ApplyCommandWindowAppearance(",
        "post-layout command appearance transform");
    ExpectContains(
        commandInterface,
        "commandWindowPosition.y + commandWindowSize.y",
        "bottom-anchored command appearance pivot");
    ExpectContains(
        commandInterface,
        "m_CommandScrollToTopRequested",
        "new command-output scroll reset");
    ExpectOrdered(
        commandInterface,
        "ImGui::SetNextWindowScroll(ImVec2(-1.f, 0.f))",
        "ImGui::Begin(",
        "same-frame command-output first-line visibility");
    ExpectAbsent(
        commandInterface,
        "ImGui::SetScrollY(0.f)",
        "removed delayed command-output scroll target");
    ExpectAbsent(
        commandInterface,
        "settingsExpanded",
        "Settings-independent command geometry");
    ExpectAbsent(
        commandInterface,
        "m_SettingsPresentation",
        "removed presented-Settings command coupling");

    const std::string_view commandWindowAppearance = ExtractSection(
        source,
        "    static void ApplyCommandWindowAppearance(",
        "    static void ApplyBackdropAppearance(",
        "command appearance geometry");
    ExpectContains(
        commandWindowAppearance,
        "vertex.pos.y =",
        "vertical command appearance transform");
    ExpectContains(
        commandWindowAppearance,
        "command.ClipRect.y =",
        "vertical command clip minimum transform");
    ExpectContains(
        commandWindowAppearance,
        "command.ClipRect.w =",
        "vertical command clip maximum transform");
    ExpectAbsent(
        commandWindowAppearance,
        "vertex.pos.x =",
        "full-width command appearance X stability");
    ExpectAbsent(
        commandWindowAppearance,
        "command.ClipRect.x =",
        "full-width command clip left stability");
    ExpectAbsent(
        commandWindowAppearance,
        "command.ClipRect.z =",
        "full-width command clip right stability");

    const std::string_view commandLaneSetup = ExtractSection(
        source,
        "        const ImGuiViewport* mainViewport =",
        "        const bool visibilityBenchmarkBusy =",
        "per-frame command lane reservation");
    ExpectContains(
        commandLaneSetup,
        "mainViewport->WorkPos.x",
        "viewport-relative command lane origin");
    ExpectContains(
        commandLaneSetup,
        "mainViewport->WorkSize.x",
        "viewport-relative command lane extent");
    ExpectContains(
        commandLaneSetup,
        "GetCommandInterfaceMinimumHeight()",
        "command minimum-height measurement");
    ExpectContains(
        commandLaneSetup,
        "GetCommandInterfaceReservedHeight()",
        "fixed three-line command reservation");
    ExpectContains(
        commandLaneSetup,
        "ImGui::GetStyle().WindowMinSize.x",
        "minimum renderable fixed command width");
    ExpectContains(
        commandLaneSetup,
        "GetSettingsCollapsedWindowHeight(\n"
        "                    ImGui::GetStyle(),\n"
        "                    ImGui::GetFontSize(),\n"
        "                    true,\n"
        "                    true)",
        "worst-case Settings minimum envelope");
    ExpectContains(
        commandLaneSetup,
        "m_CommandLayout = ResolveCommandInterfaceLayout(",
        "pure command-layout helper integration");
    ExpectContains(
        commandLaneSetup,
        "float(m_SettingsPanelMarginPixels)",
        "shared Settings and command margin");
    ExpectContains(
        commandLaneSetup,
        "m_CommandLayout.settingsMaximumBottom",
        "Settings vertical cap from command lane");
    ExpectOrdered(
        source,
        "m_CommandLayout = ResolveCommandInterfaceLayout(",
        "        const bool sceneLoading =",
        "command lane availability while loading");

    const std::string_view settingsLayout = ExtractSection(
        source,
        "        constexpr float SettingsWindowWidthInFontHeights =",
        "        const bool hasPerformanceStatus =",
        "Settings bounded root geometry");
    ExpectContains(
        settingsLayout,
        "workRectangle.minX + settingsPanelMarginPixels",
        "viewport-relative Settings left edge");
    ExpectContains(
        settingsLayout,
        "workRectangle.minY + settingsPanelMarginPixels",
        "viewport-relative Settings top edge");
    ExpectContains(
        settingsLayout,
        "settingsMaximumBottom - settingsWindowTop",
        "Settings root height cap above command lane");
    ExpectContains(
        settingsLayout,
        "settingsMaximumWindowHeight",
        "Settings root constraint integration");

    const std::string_view settingsBodyLayout = ExtractSection(
        source,
        "        const float settingsBodyMaxHeight =",
        "        PrepareSettingsScrollStability();",
        "Settings body command-lane cap");
    ExpectContains(
        settingsBodyLayout,
        "settingsMaximumBottom -",
        "Settings child height cap above command lane");
    ExpectContains(
        settingsBodyLayout,
        "ImGui::GetCursorScreenPos().y",
        "Settings child remaining-height measurement");

    ExpectContains(
        source,
        "ImVec4 panelBodySurface;",
        "shared Settings, Materials, and command body surface token");
    ExpectContains(
        source,
        "ImVec4 settingsTitleSurface;",
        "shared Settings and Materials title surface token");
    ExpectContains(
        source,
        "tokens.settingsTitleSurface = CompositeUiColorOver(\n"
        "            tokens.drawerHeader,\n"
        "            tokens.panelBodySurface);",
        "drawer-blue Settings title composition");
    ExpectContains(
        source,
        "ImGuiCol_TitleBg,\n"
        "            g_UiVisualTokens.settingsTitleSurface",
        "resting Settings title color");
    ExpectContains(
        source,
        "ImGuiCol_TitleBgActive,\n"
        "            g_UiVisualTokens.settingsTitleSurface",
        "active Settings title color");
    ExpectContains(
        source,
        "ImGuiCol_TitleBgCollapsed,\n"
        "            g_UiVisualTokens.settingsTitleSurface",
        "collapsed Settings title color");
    ExpectAbsent(
        source,
        "titleAndFooter",
        "removed misleading shared title/footer token");
    ExpectContains(
        source,
        "const float settingsPanelMarginPixels =\n"
        "            float(m_SettingsPanelMarginPixels);",
        "Settings placement shared-margin source");

    ExpectContains(
        uiCommandLayoutSource,
        "const float safeReservedHeight",
        "fixed command reservation sanitization");
    ExpectContains(
        uiCommandLayoutSource,
        "const float safeMinimumRenderableWidth",
        "fixed ImGui root minimum-width sanitization");
    ExpectContains(
        uiCommandLayoutSource,
        "const float safeMinimumSettingsHeight",
        "Settings root minimum-height sanitization");
    ExpectContains(
        uiCommandLayoutSource,
        "result.left = innerLeft;",
        "full-width command left edge");
    ExpectContains(
        uiCommandLayoutSource,
        "result.width = std::max(\n"
        "            0.f,\n"
        "            innerRight - innerLeft);",
        "margin-to-margin command width");
    ExpectContains(
        uiCommandLayoutSource,
        "result.top = result.bottom - result.height;",
        "bottom-reserved command lane");
    ExpectContains(
        uiCommandLayoutSource,
        "result.settingsMaximumBottom = std::max(",
        "Settings cap above command lane");
    ExpectContains(
        uiCommandLayoutSource,
        "result.top - safeMargin",
        "consistent vertical Settings-to-command margin");
    ExpectContains(
        uiCommandLayoutSource,
        "result.width >= safeMinimumRenderableWidth",
        "minimum renderable command width gate");
    ExpectContains(
        uiCommandLayoutSource,
        "settingsAvailableHeight >= safeMinimumSettingsHeight",
        "minimum renderable Settings height gate");
    ExpectAbsent(
        uiCommandLayoutSource,
        "preferredWidth",
        "removed command width cap");
    ExpectAbsent(
        uiCommandLayoutSource,
        "UiSettingsPresentation",
        "Settings-independent command layout helper");
    ExpectAbsent(
        uiCommandLayoutSource,
        "settings.visible",
        "visibility-independent command layout helper");
    ExpectAbsent(
        uiCommandLayoutSource,
        "settings.bounds",
        "geometry-independent command layout helper");
    ExpectContains(
        uiCommandLayoutSource,
        "horizontallyInside",
        "command edge-containment gate");

    const std::string_view uiRendererFrame = ExtractSection(
        source,
        "        buildUI();\n        DrawCommandInterface();",
        "    virtual void BackBufferResizing() override",
        "UI renderer frame ordering");
    ExpectOrdered(
        uiRendererFrame,
        "DrawCommandInterface();",
        "ImGui::Render();",
        "command interface composition");
    ExpectOrdered(
        uiRendererFrame,
        "ImGui::Render();",
        "TryApplyDeferredDropdownUiActions(true, true);",
        "older deferred UI actions before slash-command dispatch");
    ExpectOrdered(
        uiRendererFrame,
        "TryApplyDeferredDropdownUiActions(true, true);",
        "ExecuteUiCommand(command);",
        "frame-end command dispatch");
    ExpectContains(
        source,
        "bool ShouldSuppressFullscreenShortcut() const override",
        "command-interface fullscreen-shortcut suppression");
    ExpectContains(
        source,
        "return m_CommandOpen;",
        "fullscreen-shortcut suppression lifetime");
    ExpectContains(
        source,
        "key == GLFW_KEY_SLASH",
        "slash command-interface shortcut");
    ExpectContains(
        source,
        "virtual bool KeyboardCharInput(",
        "command-interface character capture");
    ExpectContains(
        source,
        "if (m_CommandOpen)",
        "command-interface shortcut isolation");
    const std::string_view commandKeyboard = ExtractSection(
        source,
        "        const bool captured = ImGui_Renderer::KeyboardUpdate(",
        "    virtual bool KeyboardCharInput(",
        "command-interface keyboard ownership");
    ExpectOrdered(
        commandKeyboard,
        "key, scancode, action, mods);",
        "const bool plainCommandShortcut =",
        "ImGui keyboard routing before slash classification");
    ExpectOrdered(
        commandKeyboard,
        "const bool plainCommandShortcut =",
        "if (m_CommandOpen)",
        "slash classification before open command ownership");
    const std::string_view commandOpenKeyboardGate = ExtractSection(
        commandKeyboard,
        "        if (m_CommandOpen)\n"
        "        {",
        "        if (key == GLFW_KEY_SLASH &&\n"
        "            action == GLFW_PRESS &&\n"
        "            plainCommandShortcut &&\n"
        "            !ImGui::GetIO().WantTextInput)",
        "open command-interface keyboard gate");
    ExpectContains(
        commandOpenKeyboardGate,
        "return true;",
        "open command interface captures every key including plain F and V");
    ExpectContains(
        commandOpenKeyboardGate,
        "key == GLFW_KEY_SLASH",
        "plain slash closes the open command interface");
    ExpectContains(
        commandOpenKeyboardGate,
        "m_CommandOpen = false;",
        "slash command close target");
    ExpectContains(
        commandOpenKeyboardGate,
        "m_CommandFocusRequested = false;",
        "slash command focus release");
    ExpectContains(
        commandOpenKeyboardGate,
        "m_SuppressCommandShortcutSlashCharacter = true;",
        "closing slash character suppression");
    ExpectContains(
        commandOpenKeyboardGate,
        "ImGui::ClearActiveID();",
        "closing slash active-text release");
    ExpectAbsent(
        commandOpenKeyboardGate,
        "GLFW_KEY_ESCAPE",
        "Escape-independent command lifetime");
    ExpectAbsent(
        commandOpenKeyboardGate,
        "m_ui.ShowUI",
        "open command isolation from Settings visibility");
    const std::string_view commandOpeningShortcut = ExtractSection(
        commandKeyboard,
        "        if (key == GLFW_KEY_SLASH &&\n"
        "            action == GLFW_PRESS &&\n"
        "            plainCommandShortcut &&\n"
        "            !ImGui::GetIO().WantTextInput)",
        "        if (key == GLFW_KEY_ESCAPE &&",
        "closed command-interface slash shortcut");
    ExpectContains(
        commandOpeningShortcut,
        "m_CommandOpen = true;",
        "slash command open target");
    ExpectContains(
        commandOpeningShortcut,
        "m_CommandFocusRequested = true;",
        "slash command focus request");
    ExpectContains(
        commandOpeningShortcut,
        "m_CommandScrollToTopRequested = true;",
        "slash command result scroll reset");
    ExpectContains(
        commandOpeningShortcut,
        "m_SuppressCommandShortcutSlashCharacter = true;",
        "opening slash character suppression");
    if (CountOccurrences(
            commandKeyboard,
            "m_SuppressCommandShortcutSlashCharacter = true;") != 2u)
    {
        Fail("opening and closing slash shortcuts must each suppress their "
            "paired character event exactly once.");
    }
    ExpectAbsent(
        commandKeyboard,
        "ImGuiInputTextFlags_EscapeClearsAll",
        "native Escape edit cancellation");
    const std::string_view commandCharacterInput = ExtractSection(
        source,
        "    virtual bool KeyboardCharInput(",
        "    virtual void buildUI(void) override",
        "command-interface character ownership");
    ExpectOrdered(
        commandCharacterInput,
        "if (m_SuppressCommandShortcutSlashCharacter)",
        "if (m_CommandOpen)",
        "shortcut slash suppression before command character capture");
    ExpectContains(
        commandCharacterInput,
        "m_SuppressCommandShortcutSlashCharacter = false;",
        "one-shot shortcut slash suppression");
    ExpectContains(
        commandCharacterInput,
        "unicode == static_cast<unsigned int>('/')",
        "paired slash character identity");
    ExpectContains(
        commandCharacterInput,
        "ImGui_Renderer::KeyboardCharInput(unicode, mods);",
        "ordinary command character routing");
    const std::string_view materialInspectorShortcut = ExtractSection(
        commandKeyboard,
        "        const bool plainMaterialEditorShortcut =",
        "        return captured;",
        "center Material Inspector shortcut");
    ExpectContains(
        materialInspectorShortcut,
        "key == GLFW_KEY_M",
        "center Material Inspector shortcut");
    ExpectOrdered(
        commandKeyboard,
        "if (m_CommandOpen)",
        "key == GLFW_KEY_M",
        "command ownership before center Material Inspector shortcut");
    ExpectOrdered(
        commandKeyboard,
        "key == GLFW_KEY_SLASH",
        "key == GLFW_KEY_M",
        "slash handling before center Material Inspector shortcut");
    ExpectOrdered(
        materialInspectorShortcut,
        "!ImGui::GetIO().WantTextInput",
        "m_app->ToggleCenterMaterialInspector();",
        "text-input gate before center Material Inspector shortcut");
    ExpectAbsent(
        materialInspectorShortcut,
        "m_ui.ShowMaterialEditor =",
        "typed center Material Inspector shortcut");
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
    if (CountOccurrences(
            imguiOverride,
            "const bool is_stacked_panel_title =") != 2u)
    {
        Fail("expanded and collapsed stacked-panel decorations must each keep "
            "one skin-independent title identity.");
    }
    const std::string_view collapsedSettingsTitle = ExtractSection(
        imguiOverride,
        "// Title bar only for ordinary collapsed windows.",
        "g.Style.FrameBorderSize = backup_border_size;",
        "collapsed Settings title frame");
    ExpectContains(
        collapsedSettingsTitle,
        "if (!is_stacked_panel_title)",
        "collapsed stacked-panel native frame-border isolation");
    ExpectContains(
        collapsedSettingsTitle,
        "is_stacked_panel_title\n"
        "+                ? style.FrameRounding",
        "collapsed stacked-panel drawer-header rounding");
    ExpectContains(
        collapsedSettingsTitle,
        "!is_stacked_panel_title",
        "collapsed stacked-panel skin-matched drawer-header border policy");
    const std::string_view expandedSettingsTitle = ExtractSection(
        imguiOverride,
        "// Use the same frame radius and outline path as a resting",
        "if (has_expanded_stacked_panel_body)",
        "expanded stacked-panel title frame");
    ExpectContains(
        expandedSettingsTitle,
        "RenderFrame(",
        "expanded Settings drawer-header outline path");
    ExpectContains(
        expandedSettingsTitle,
        "style.FrameRounding",
        "expanded Settings drawer-header rounding");
    ExpectContains(
        expandedSettingsTitle,
        "!is_stacked_panel_window",
        "expanded stacked-panel skin-matched drawer-header border policy");
    ExpectAbsent(
        expandedSettingsTitle,
        "RenderGradientFrameOutline(",
        "expanded Settings duplicate title outline");
    ExpectContains(
        expandedSettingsTitle,
        "window_rounding,\n"
        "+                    ImDrawFlags_RoundCornersTop",
        "ordinary expanded window title fallback");
    ExpectAbsent(
        imguiRuntimePolicy,
        "is_stacked_panel_title",
        "skin-independent stacked-panel title identity");
    constexpr std::string_view sharedStackedPanelIdentity =
        "(strcmp(window->Name, \"Settings\") == 0 || strcmp(window->Name, \"Materials\") == 0)";
    if (CountOccurrences(imguiOverride, sharedStackedPanelIdentity) != 4u)
    {
        Fail("expanded/collapsed stacked-panel decorations must keep four "
            "shared Settings and Material identities.");
    }
    if (CountOccurrences(
            imguiOverride,
            "const bool is_stacked_panel_window =") != 2u)
    {
        Fail("outer-border and expanded-body decoration must each keep one "
            "shared stacked-panel window identity.");
    }
    if (CountOccurrences(
            imguiRuntimePolicy,
            "const bool is_stacked_panel_window =") != 2u ||
        CountOccurrences(imguiRuntimePolicy, sharedStackedPanelIdentity) != 2u)
    {
        Fail("OG stock-widget policy must guard both stacked-panel window "
            "decoration sites.");
    }
    ExpectContains(
        imguiRuntimePolicy,
        "const bool is_stacked_panel_window =\n"
        "+        !g.UvsrStockWidgetRendering &&\n"
         "         (strcmp(window->Name, \"Settings\") == 0 || strcmp(window->Name, \"Materials\") == 0);",
        "OG stock-widget outer-border bypass");
    ExpectContains(
        imguiRuntimePolicy,
        "        const bool is_stacked_panel_window =\n"
        "+            !g.UvsrStockWidgetRendering &&\n"
        "             (strcmp(window->Name, \"Settings\") == 0 || strcmp(window->Name, \"Materials\") == 0);",
        "OG stock-widget expanded-body bypass");
    ExpectAbsent(
        imguiOverride,
        "\"Material Editor\"",
        "retired Material Editor ImGui window identity");
    ExpectAbsent(
        imguiRuntimePolicy,
        "\"Material Editor\"",
        "retired Material Editor runtime-policy identity");
    ExpectContains(
        source,
        "constexpr size_t UiBackdropRectCount = 5u;",
        "split Settings, material, and command backdrop masks");
    ExpectContains(
        source,
        "constexpr size_t UiMaterialTitleBackdropIndex = 2u;",
        "stable Material title backdrop slot");
    ExpectContains(
        source,
        "constexpr size_t UiMaterialBodyBackdropIndex = 3u;",
        "stable Material body backdrop slot");
    ExpectContains(
        source,
        "constexpr size_t UiCommandBackdropIndex = 4u;",
        "stable command backdrop slot");
    const std::string_view collapsedSettingsBackdrops = ExtractSection(
        source,
        "        if (settingsCollapsed)\n"
        "        {",
        "        else\n"
        "        {",
        "collapsed Settings backdrop geometry");
    ExpectContains(
        collapsedSettingsBackdrops,
        "titleBackdrop.minX = settingsWindowPosition.x + 0.5f;",
        "collapsed Settings title left inset");
    ExpectContains(
        collapsedSettingsBackdrops,
        "titleBackdrop.minY = settingsWindowPosition.y + 0.5f;",
        "collapsed Settings title top inset");
    ExpectContains(
        collapsedSettingsBackdrops,
        "settingsWindowPosition.x + settingsWindowSize.x - 0.5f",
        "collapsed Settings right inset");
    ExpectContains(
        collapsedSettingsBackdrops,
        "settingsWindowPosition.y + settingsTitleHeight - 0.5f",
        "collapsed Settings title bottom inset");
    ExpectContains(
        collapsedSettingsBackdrops,
        "titleBackdrop.rounding = style.FrameRounding;",
        "collapsed Settings drawer-header blur radius");
    ExpectContains(
        collapsedSettingsBackdrops,
        "UiBackdropRect& statusBackdrop =",
        "collapsed Settings status blur mask");
    ExpectContains(
        collapsedSettingsBackdrops,
        "settingsWindowPosition.y + settingsWindowSize.y - 0.5f",
        "collapsed Settings status bottom inset");
    const std::string_view expandedSettingsBackdrops = ExtractSection(
        source,
        "            // Match the two actual rounded surfaces drawn by the ImGui",
        "        constexpr size_t settingsBackdropCount = 2u",
        "expanded Settings backdrop geometry");
    ExpectContains(
        expandedSettingsBackdrops,
        "titleBackdrop.rounding = style.FrameRounding;",
        "expanded Settings drawer-header blur radius");
    ExpectContains(
        expandedSettingsBackdrops,
        "UiBackdropRect& bodyBackdrop =",
        "expanded Settings body blur mask");
    if (CountOccurrences(
            source,
            "titleBackdrop.rounding = style.FrameRounding;") != 2u)
    {
        Fail("expanded and collapsed Settings titles must each use the "
            "drawer-header frame radius.");
    }
    ExpectAbsent(
        source,
        "titleBackdrop.rounding = style.WindowRounding;",
        "removed window-radius Settings title masks");
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
        "g_UiVisualTokens.backdropShadowBlur",
        "Settings shadow blur");
    ExpectContains(
        source,
        "g_UiVisualTokens.backdropShadowOpacity",
        "Settings shadow opacity");
    ExpectContains(
        source,
        "g_UiVisualTokens.backdropShadowOffsetY",
        "Settings shadow offset");
    ExpectContains(
        source,
        "const bool hasVisibleShadow = std::any_of(",
        "shadow-only panel presentation gate");
    ExpectContains(
        source,
        "(!renderBackdrop && !hasVisibleShadow)",
        "shadow-only backdrop-pass lifetime");
    ExpectContains(
        source,
        "backdropEnabled ? UiBackgroundBlurPixels : 0.f",
        "OG shadow-only backdrop dispatch");
    ExpectAbsent(
        source,
        "tokens.backdropShadowBlur = 0.f",
        "OG zoom-matched shadow retention");
    const std::string_view commandBackdropAppearance = ExtractSection(
        source,
        "    static void ApplyCommandBackdropAppearance(",
        "    static void ApplyBackdropAppearance(",
        "command backdrop appearance transform");
    ExpectContains(
        commandBackdropAppearance,
        "backdropRect.minY =",
        "command backdrop vertical transform");
    ExpectContains(
        commandBackdropAppearance,
        "backdropRect.maxY =",
        "command backdrop vertical transform");
    ExpectContains(
        commandBackdropAppearance,
        "backdropRect.opacity =",
        "command backdrop opacity transform");
    ExpectAbsent(
        commandBackdropAppearance,
        "backdropRect.minX",
        "fixed command backdrop left edge");
    ExpectAbsent(
        commandBackdropAppearance,
        "backdropRect.maxX",
        "fixed command backdrop right edge");
    ExpectContains(
        commandInterface,
        "m_ui.BackdropRects[UiCommandBackdropIndex]",
        "shared Amp and OG command backdrop slot");
    ExpectAbsent(
        commandInterface,
        "m_ComposedUiSkin == UiSkin::Og",
        "skin-independent command backdrop registration");
    ExpectAbsent(
        commandInterface,
        "m_ComposedUiSkin == UiSkin::Amp",
        "skin-independent command backdrop registration");
    if (CountOccurrences(
            commandInterface,
            "m_ui.BackdropRects[UiCommandBackdropIndex]") != 1u)
    {
        Fail("Amp and OG must share exactly one command backdrop slot.");
    }
    ExpectOrdered(
        commandInterface,
        "CaptureCurrentWindowBackdrop(",
        "ApplyCommandBackdropAppearance(",
        "command backdrop registration before appearance transform");
    ExpectContains(
        commandInterface,
        "commandBackdrop.shadowBlur =",
        "shared command shadow blur");
    ExpectContains(
        commandInterface,
        "commandBackdrop.shadowOpacity =",
        "shared command shadow opacity");
    ExpectContains(
        commandInterface,
        "commandBackdrop.shadowOffsetY =",
        "shared command shadow offset");
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
    ExpectContains(
        backdropShader,
        "const float neutralLuminance = dot(\n"
        "        outputColor.rgb,\n"
        "        float3(0.2126, 0.7152, 0.0722));",
        "neutral Amp backdrop luminance");
    ExpectContains(
        backdropShader,
        "outputColor.rgb = neutralLuminance.xxx;",
        "monotone Amp backdrop composite");
    const std::string_view retainedStatusSurface = ExtractSection(
        imguiOverride,
        "Paint that retained area with the same WindowBg surface used while",
        "// Title bar only for ordinary collapsed windows.",
        "collapsed Settings retained status surface");
    ExpectOrdered(
        retainedStatusSurface,
        "GetColorU32(GetWindowBgColorIdx(window))",
        "ImGuiNextWindowDataFlags_HasBgAlpha",
        "collapsed Settings background-alpha override");
    ExpectOrdered(
        retainedStatusSurface,
        "ImGuiNextWindowDataFlags_HasBgAlpha",
        "g.NextWindowData.BgAlphaVal",
        "collapsed Settings explicit background alpha");
    ExpectOrdered(
        retainedStatusSurface,
        "g.NextWindowData.BgAlphaVal",
        "window->DrawList->AddRectFilled(",
        "collapsed Settings applies alpha before drawing");

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
        "const auto& lights = m_app->GetEditableLights();",
        "##SkyBody",
        "Sky drawer");
    ExpectDrawerContract(
        source,
        "const bool lightsOpen = DrawCollapsingHeader(",
        "const bool shadowsOpen = DrawCollapsingHeader(",
        "##LightsBody",
        "Lights drawer");
    ExpectDrawerContract(
        source,
        "const bool shadowsOpen = DrawCollapsingHeader(",
        "constexpr float ActionButtonCount = 4.f;",
        "##ShadowsBody",
        "Shadows drawer");

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
    const std::string_view visibilityResolutionOrder = ExtractSection(
        visibility,
        "resolutionOrder = {",
        "for (const VisibilityResolution resolution : resolutionOrder)",
        "Visibility sampling-resolution expense order");
    ExpectOrdered(
        visibilityResolutionOrder,
        "VisibilityResolution::Quarter",
        "VisibilityResolution::Half",
        "Visibility sampling-resolution expense order");
    ExpectOrdered(
        visibilityResolutionOrder,
        "VisibilityResolution::Half",
        "VisibilityResolution::Full",
        "Visibility sampling-resolution expense order");
    ExpectAbsent(
        visibility,
        "\"Sample Count Mode\"",
        "retired Visibility sample-count mode");
    ExpectAbsent(
        source,
        "Filter-Adapted Spatiotemporal Noise",
        "retired Visibility offline-noise UI label");
    ExpectAbsent(
        source,
        "VisibilitySampleSpecialization::Fixed",
        "retired Visibility fixed-count specialization");
    ExpectAbsent(
        source,
        "VisibilitySampleSpecialization::Generic",
        "retired Visibility generic specialization");
    ExpectContains(
        visibility,
        "DrawSliderInt(",
        "Visibility sample-count slider");
    ExpectContains(
        visibility,
        "\"Samples##VisibilitySamples\"",
        "Visibility shared sample-count slider");
    ExpectContains(
        visibility,
        "\"Factory default: full resolution, 20 samples, Toroidal Blue \"",
        "High visibility profile precision description");
    ExpectContains(
        source,
        "\"Low, Medium, and High begin at Performance Precision; Ultra \"",
        "Visibility buffer precision preset explanation");
    ExpectContains(
        visibility,
        "!ImGui::IsItemActive()",
        "Visibility sample slider inactive-or-release commit");
    ExpectAbsent(
        visibility,
        "\"##VisibilityRuntimeSampleCountControls\"",
        "removed runtime-only sample controls");
    ExpectAbsent(
        visibility,
        "\"Runtime Samples##VisibilityRuntimeSamples\"",
        "duplicate Runtime-only sample slider");
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
        "\"Distribution\"",
        "Visibility distribution label");
    const std::string_view sharedVisibilitySampling = ExtractSection(
        visibility,
        "\"Shared Visibility Sampling\"",
        "\"Ambient Occlusion\"",
        "Shared Visibility Sampling panel");
    ExpectContains(
        sharedVisibilitySampling,
        "\"Distribution\"",
        "Shared Visibility Sampling production controls");
    ExpectAbsent(
        sharedVisibilitySampling,
        "\"Sample Count Mode\"",
        "Shared Visibility Sampling developer sample-count mode");
    ExpectOrdered(
        sharedVisibilitySampling,
        "\"Thickness\"",
        "\"Distribution\"",
        "Visibility Distribution placement");
    ExpectAbsent(
        visibility,
        "\"Developer Options##VisibilityDeveloperOptions\"",
        "retired Visibility Developer Options panel");
    ExpectAbsent(
        visibility,
        "\"Radial Distribution Exponent\"",
        "Visibility distribution label");
    ExpectAbsent(
        visibility,
        "\"Include Emissive Sources\"",
        "retired emissive GI source control");
    ExpectAbsent(
        visibility,
        "\"Emissive Source Gain\"",
        "retired emissive GI source gain");
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
        "running without wall-clock ",
        "uncapped AA motion benchmark tooltip");
    ExpectContains(
        statistics,
        "\"pacing.\"",
        "uncapped AA motion benchmark tooltip");
    ExpectAbsent(
        statistics,
        "40 Hz",
        "retired AA motion benchmark pacing");
    ExpectContains(
        statistics,
        "PushID(\"StatisticsControls\")",
        "Statistics drawer");
    ExpectContains(
        statistics,
        "StatisticsEffect::AntiAliasing",
        "Anti-Aliasing statistics selector");
    for (const std::pair<std::string_view, std::string_view>& effect : {
            std::pair<std::string_view, std::string_view>{
                "StatisticsEffect::ScreenSpaceShadows",
                "\"Screen-Space Shadows\""},
            {
                "StatisticsEffect::SparseVirtualShadowMaps",
                "\"Sparse Virtual Shadow Maps\""},
            {
                "StatisticsEffect::DiagnosticCascadedShadowMaps",
                "\"Diagnostic Cascaded Shadow Maps\""},
            {
                "StatisticsEffect::EnvironmentBackground",
                "\"Environment Background\""} })
    {
        ExpectContains(
            statistics,
            effect.first,
            "integrated shadow and environment statistics selector");
        ExpectContains(
            statistics,
            effect.second,
            "integrated shadow and environment statistics label");
    }
    ExpectContains(
        statistics,
        "m_ScreenSpaceShadowStatLines",
        "screen-space shadow statistics presentation");
    ExpectContains(
        statistics,
        "m_SparseShadowStatLines",
        "SVSM statistics presentation");
    ExpectContains(
        statistics,
        "m_DiagnosticCsmStatLines",
        "diagnostic CSM statistics presentation");
    ExpectContains(
        statistics,
        "GetImageBasedLightingSourceAverageLuminance()",
        "environment source statistics");
    ExpectContains(
        statistics,
        "GetImageBasedLightingRadianceScale()",
        "environment exposure statistics");
    ExpectContains(
        statistics,
        "m_ui.IsDiffuseAmbientFillActive()",
        "diffuse IBL statistics");
    ExpectContains(
        statistics,
        "m_ui.IsSpecularAmbientFillActive()",
        "specular IBL statistics");
    const std::string_view rendererStatisticsTable = ExtractSection(
        statistics,
        "if (ImGui::BeginTable(\n"
        "                        \"##RendererLiveTimings\",",
        "ImGui::EndTable();",
        "renderer live statistics table");
    ExpectContains(
        rendererStatisticsTable,
        "\"Current\"",
        "renderer live statistics value column");
    for (const std::string_view radianceLabel : {
            std::string_view("\"Source Mean Radiance\""),
            std::string_view("\"Exposed Mean Radiance\""),
            std::string_view("\"Diffuse IBL Mean Radiance\""),
            std::string_view("\"Specular IBL Mean Radiance\"") })
    {
        ExpectContains(
            rendererStatisticsTable,
            radianceLabel,
            "IBL radiance row placement");
    }
    ExpectContains(
        rendererStatisticsTable,
        "ImGui::TableSetColumnIndex(1);",
        "IBL radiance current-value placement");
    ExpectAbsent(
        statistics,
        "\"Diffuse %.4f / Specular %.4f\"",
        "removed combined IBL statistics text");
    ExpectAbsent(
        statistics,
        "\"Procedural Sky\"",
        "removed procedural-sky statistics label");
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
        "\"Effective Temporal Cost\"",
        "Statistics ownership of Effective Temporal Cost");
    ExpectContains(
        statistics,
        "m_app->GetTemporalAATimings()",
        "runtime source of Effective Temporal Cost");
    const std::string_view effectiveTemporalCostStatistics =
        ExtractSection(
            statistics,
            "\"Effective Temporal Cost\"",
            "\"Temporal Dispatches\"",
            "Effective Temporal Cost statistics row");
    ExpectContains(
        effectiveTemporalCostStatistics,
        "GetTemporalAaCostModeLabel(",
        "Effective Temporal Cost label resolver");
    ExpectContains(
        effectiveTemporalCostStatistics,
        "effectiveCostMode",
        "timing-derived Effective Temporal Cost value");
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
        "candidateLabel += \"##MethodCandidate\";",
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
        "candidateLabel += \"##QualityCandidate\";",
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
    ExpectAbsent(
        aliasing,
        "###MethodCandidate",
        "Aliasing Method options with shared ImGui IDs");
    ExpectAbsent(
        aliasing,
        "###QualityCandidate",
        "Aliasing Quality options with shared ImGui IDs");
    ExpectAbsent(
        aliasing,
        "###MorphologyCandidate",
        "Aliasing Morphology options with shared ImGui IDs");
    ExpectAbsent(
        aliasing,
        "###Stable Interior",
        "Aliasing Stable Interior options with shared ImGui IDs");
    ExpectAbsent(
        source,
        "###VisibilitySampleCountMode",
        "Visibility Sample Count Mode options with shared ImGui IDs");
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
        "\"##AliasingEnabledControls\"",
        "Aliasing enabled selector gate");
    ExpectOrdered(
        aliasing,
        "ImGui::Checkbox(\"Enabled\", &selectorSettings.enabled);",
        "const bool methodComboOpen = BeginRoundedCombo(",
        "Aliasing method remains an ordinary sibling while bypassed");
    const std::string_view methodOrder = ExtractSection(
        aliasing,
        "methodOrder = {",
        "for (const AntiAliasingMethod candidate : methodOrder)",
        "Aliasing Method expense order");
    ExpectOrdered(
        methodOrder,
        "AntiAliasingMethod::IntelCmaa2",
        "TemporalSubpixelMorphological",
        "Aliasing Method expense order");
    ExpectOrdered(
        methodOrder,
        "TemporalSubpixelMorphological",
        "AntiAliasingMethod::Msaa",
        "Aliasing Method expense order");
    ExpectContains(
        aliasing,
        "const std::string preview = inherited",
        "resolved concrete inherited-value preview");
    ExpectContains(
        aliasing,
        "candidateLabel == inheritedOrAutoValue",
        "resolved concrete inherited-value row selection");
    ExpectAbsent(
        aliasing,
        "\"Preset\"",
        "visible neutral Preset entry");
    for (const std::string_view normalTaaControl : {
            std::string_view("\"Dejitter##AliasingDejitter\""),
            std::string_view("\"Motion Source\""),
            std::string_view("\"Reconstruction\""),
            std::string_view("\"Morphology##Developer\"") })
    {
        ExpectContains(
            aliasing,
            normalTaaControl,
            "normal production TAA controls");
    }
    ExpectAbsent(
        aliasing,
        "\"Subpixel Morphology##Developer\"",
        "retired Subpixel Morphology label");
    ExpectAbsent(
        aliasing,
        "\"Current Reconstruction\"",
        "removed Current Reconstruction dropdown");
    ExpectAbsent(
        aliasing,
        "\"History Reconstruction\"",
        "renamed Reconstruction dropdown");
    const std::string_view qualityOrder = ExtractSection(
        aliasing,
        "qualityOrder = {",
        "for (const AntiAliasingQuality candidate : qualityOrder)",
        "Aliasing Quality expense order");
    ExpectOrdered(
        qualityOrder,
        "AntiAliasingQuality::Low",
        "AntiAliasingQuality::Medium",
        "Aliasing Quality expense order");
    ExpectOrdered(
        qualityOrder,
        "AntiAliasingQuality::Medium",
        "AntiAliasingQuality::High",
        "Aliasing Quality expense order");
    ExpectOrdered(
        qualityOrder,
        "AntiAliasingQuality::High",
        "AntiAliasingQuality::Ultra",
        "Aliasing Quality expense order");
    const std::string_view morphologyOrder = ExtractSection(
        aliasing,
        "morphologyQualityOrder = {",
        "for (const AntiAliasingQuality candidateQuality :",
        "Aliasing Morphology expense order");
    ExpectOrdered(
        morphologyOrder,
        "AntiAliasingQuality::Low",
        "AntiAliasingQuality::Medium",
        "Aliasing Morphology expense order");
    ExpectOrdered(
        morphologyOrder,
        "AntiAliasingQuality::Medium",
        "AntiAliasingQuality::High",
        "Aliasing Morphology expense order");
    ExpectOrdered(
        morphologyOrder,
        "AntiAliasingQuality::High",
        "AntiAliasingQuality::Ultra",
        "Aliasing Morphology expense order");
    ExpectOrdered(
        aliasing,
        "\"Off##MorphologyCandidate\"",
        "morphologyQualityOrder)",
        "Aliasing Morphology expense order");
    ExpectAbsent(
        aliasing,
        "\"Preset##MorphologyCandidate\"",
        "Aliasing Morphology neutral Preset row");
    const std::string_view motionSourceOrder = ExtractSection(
        aliasing,
        "motionSourceOrder = {",
        "reconstructionOrder = {",
        "TAA Motion Source expense order");
    ExpectAbsent(
        motionSourceOrder,
        "FromPreset",
        "TAA Motion Source neutral Preset row");
    ExpectOrdered(
        motionSourceOrder,
        "TemporalAaMotionSourceOverride::Center",
        "ClosestCross",
        "TAA Motion Source expense order");
    ExpectOrdered(
        motionSourceOrder,
        "ClosestCross",
        "CenterFirstEdgeDilation",
        "TAA Motion Source expense order");
    const std::string_view rectificationOrder = ExtractSection(
        aliasing,
        "rectificationOrder = {",
        "drawEnumOption(",
        "TAA Rectification expense order");
    ExpectAbsent(
        rectificationOrder,
        "FromPreset",
        "TAA Rectification neutral Preset row");
    ExpectOrdered(
        rectificationOrder,
        "PairRgb",
        "VarianceYCoCg",
        "TAA Rectification expense order");
    ExpectAbsent(
        rectificationOrder,
        "PerPixel",
        "retired TAA per-pixel rectification choices");
    const std::string_view reconstructionOrder = ExtractSection(
        aliasing,
        "reconstructionOrder = {",
        "historyStorageOrder = {",
        "TAA Reconstruction expense order");
    ExpectAbsent(
        reconstructionOrder,
        "FromPreset",
        "TAA Reconstruction neutral Preset row");
    ExpectOrdered(
        reconstructionOrder,
        "TemporalAaHistoryFilterOverride::Bilinear",
        "OneSampleBicubic",
        "TAA Reconstruction expense order");
    ExpectOrdered(
        reconstructionOrder,
        "OneSampleBicubic",
        "FiveTapCatmullRom",
        "TAA Reconstruction expense order");
    ExpectOrdered(
        reconstructionOrder,
        "FiveTapCatmullRom",
        "NineTapCatmullRom",
        "TAA Reconstruction expense order");
    const std::string_view resurrectionOrder = ExtractSection(
        aliasing,
        "sampleResurrectionOrder = {",
        "#endif",
        "TAA Sample Resurrection expense order");
    ExpectAbsent(
        resurrectionOrder,
        "FromPreset",
        "TAA Sample Resurrection neutral Preset row");
    ExpectOrdered(
        resurrectionOrder,
        "TemporalAaSampleResurrectionOverride::Off",
        "OneOlderFrame",
        "TAA Sample Resurrection expense order");
    ExpectOrdered(
        resurrectionOrder,
        "OneOlderFrame",
        "TwoOlderFrames",
        "TAA Sample Resurrection expense order");
    ExpectContains(
        aliasing,
        "&historyFrames,\n"
        "                            1,\n"
        "                            32,",
        "32-frame TAA history range");
    ExpectContains(
        aliasing,
        "&historyStrength,\n"
        "                            0.f,\n"
        "                            200.f,",
        "200-percent TAA history-strength range");
    ExpectContains(
        aliasing,
        "ImGui::SetItemTooltip(\n"
        "                        \"%s\",\n"
        "                        \"Scale accepted history",
        "literal-safe History Strength tooltip");
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
    for (const std::string_view developerBehaviorControl : {
            std::string_view("\"History Storage\""),
            std::string_view("\"Previous-Depth Validation\""),
            std::string_view("\"History Weight\""),
            std::string_view("\"Motion Trust\""),
            std::string_view("\"Rectification Clip\""),
            std::string_view("\"Blend Domain\""),
            std::string_view("\"Sharpness Policy\""),
            std::string_view("\"Sample Resurrection\"") })
    {
        ExpectContains(
            aliasing,
            developerBehaviorControl,
            "Aliasing Developer Options image controls");
    }
    for (const std::string_view developerTopologyControl : {
            std::string_view("\"Execution Topology\""),
            std::string_view("\"Execution Path\""),
            std::string_view("\"Compute Kernel\""),
            std::string_view("\"LDS Layout\""),
            std::string_view("\"Shared-Work Reuse\""),
            std::string_view("\"Early History Rejection\""),
            std::string_view("\"Pass Fusion\""),
            std::string_view("\"Cache Blocking\"") })
    {
        ExpectAbsent(
            aliasing,
            developerTopologyControl,
            "removed Aliasing Developer Options topology presentation");
    }
    for (const std::string_view removedPerformanceControl : {
            std::string_view("\"Debug View\""),
            std::string_view("\"Stable Interior\"") })
    {
        ExpectAbsent(
            aliasing,
            removedPerformanceControl,
            "removed Aliasing developer dropdown");
    }
    ExpectAbsent(
        aliasing,
        "\"Developer Performance Overrides\"",
        "removed Aliasing developer performance drawer");
    ExpectOrdered(
        aliasing,
        "drawDejitterControl();",
        "\"Sharpness###Sharpness\"",
        "Dejitter and Sharpness toggle order");
    ExpectOrdered(
        aliasing,
        "\"Sharpness###Sharpness\"",
        "drawMorphologyOption();",
        "toggle-first Aliasing Developer Options order");
    ExpectContains(
        aliasing,
        "\"Developer Options##AliasingDeveloperOptions\"",
        "Aliasing Developer Options panel");
    const std::string_view aliasingDeveloperOptions = ExtractSection(
        aliasing,
        "\"Developer Options##AliasingDeveloperOptions\"",
        "EndAnimatedTreeNode();",
        "Aliasing Developer Options panel");
    ExpectAbsent(
        aliasing,
        "\"Effective Temporal Cost\"",
        "Statistics-only Effective Temporal Cost ownership");
    ExpectAbsent(
        aliasing,
        "m_app->GetTemporalAATimings()",
        "Statistics-only temporal timing ownership");
    ExpectContains(
        aliasing,
        "BeginRoundedCombo(\n"
        "                        \"Temporal Cost\"",
        "editable authoritative Temporal Cost selector");
    ExpectAbsent(
        aliasing,
        "\"Requested Temporal Cost\"",
        "retired verbose Temporal Cost label");
    const std::string_view aliasingPrimaryControls = ExtractSection(
        aliasing,
        "if (temporalMethodSelected)",
        "\"Developer Options##AliasingDeveloperOptions\"",
        "Aliasing primary controls");
    ExpectContains(
        aliasingPrimaryControls,
        "\"Temporal Cost\"",
        "primary Temporal Cost selector");
    ExpectContains(
        aliasingPrimaryControls,
        "defaultAliasingSettings.temporalCostMode",
        "Temporal Cost reset follows the startup default");
    ExpectAbsent(
        aliasingPrimaryControls,
        "\"History Frames\"",
        "History Frames moved out of primary Aliasing controls");
    ExpectAbsent(
        aliasingPrimaryControls,
        "\"History Strength\"",
        "History Strength moved out of primary Aliasing controls");
    ExpectContains(
        aliasingDeveloperOptions,
        "\"History Frames\"",
        "History Frames Developer Options placement");
    ExpectContains(
        aliasingDeveloperOptions,
        "\"History Strength\"",
        "History Strength Developer Options placement");
    ExpectOrdered(
        aliasingDeveloperOptions,
        "\"Sharpness###Sharpness\"",
        "\"History Frames\"",
        "toggle-first History Frames placement");
    ExpectOrdered(
        aliasingDeveloperOptions,
        "\"History Frames\"",
        "\"History Strength\"",
        "temporal history control order");
    ExpectOrdered(
        aliasingDeveloperOptions,
        "\"History Strength\"",
        "drawMorphologyOption();",
        "history controls precede remaining dropdown tuning");
    for (const std::string_view removedPanelCopy : {
            std::string_view("\"Runtime State\""),
            std::string_view("\"Image And Stability\""),
            std::string_view(
                "\"Best first comparison for small-edge swimming"),
            std::string_view("\"Effective Temporal Cost\""),
            std::string_view("m_app->GetTemporalAATimings()"),
            std::string_view("drawStatusOption") })
    {
        ExpectAbsent(
            aliasingDeveloperOptions,
            removedPanelCopy,
            "removed noninteractive Aliasing Developer Options copy");
    }
    ExpectContains(
        aliasingDeveloperOptions,
        "drawRectificationControl();",
        "Rectification Developer Options placement");
    ExpectContains(
        aliasingDeveloperOptions,
        "\"##TemporalSharpnessAvailability\",\n"
        "                            resolvedCurrent.sharpeningAllowed",
        "Sharpness policy animated availability gate");
    ExpectOrdered(
        aliasingDeveloperOptions,
        "\"Reconstruction\"",
        "drawRectificationControl();",
        "Aliasing Developer Options algorithm order");
    ExpectOrdered(
        aliasingDeveloperOptions,
        "\"History Storage\"",
        "\"Previous-Depth Validation\"",
        "Aliasing Developer Options stability order");
    ExpectOrdered(
        aliasingDeveloperOptions,
        "\"Previous-Depth Validation\"",
        "\"History Weight\"",
        "Aliasing Developer Options diagnosis order");
    ExpectContains(
        aliasingDeveloperOptions,
        "\"##SampleResurrectionAvailability\",\n"
        "                            resolvedCurrent.temporalCostMode ==\n"
        "                                TemporalAaCostMode::FullQuality",
        "Sample Resurrection animated Full Quality gate");
    for (const std::string_view removedGatedStatus : {
            std::string_view("\"Off - Requires Full Quality\""),
            std::string_view(
                "\"Unavailable in Factory Shader Build\"") })
    {
        ExpectAbsent(
            aliasingDeveloperOptions,
            removedGatedStatus,
            "collapsed Sample Resurrection status row");
    }
    ExpectAbsent(
        aliasingDeveloperOptions,
        "TemporalAaPerformanceOverrides& performanceOverrides",
        "removed Aliasing performance topology surface");
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
        "const std::string preview = inherited",
        "resolved Aliasing inherited-value previews");
    ExpectContains(
        aliasing,
        "const bool candidateRepresentsInherited =",
        "resolved Aliasing inherited row selection");
    ExpectContains(
        aliasing,
        "*selectedValuePointer =\n"
        "                                static_cast<ValueType>(0u);",
        "Aliasing reset restores inheritance");
    ExpectContains(
        aliasing,
        "const ResolvedAntiAliasingSettings resolvedCurrent =",
        "resolved Aliasing morphology presentation");
    ExpectContains(
        aliasing,
        "const bool morphologyOff =",
        "explicit Aliasing morphology-off presentation");
    ExpectContains(
        aliasingMethodControl,
        "NormalizeRedundantAntiAliasingOverrides(",
        "explicit staged Aliasing normalization");
    ExpectContains(
        source,
        "TemporalAaSharpenEnabled = false;",
        "default-disabled Aliasing sharpness");
    const std::string_view temporalPresentationSharpen = ExtractSection(
        source,
        "const bool deferTemporalSharpenToPresentation =",
        "if (m_TemporalAAPass)",
        "temporal presentation sharpening");
    ExpectContains(
        temporalPresentationSharpen,
        "TemporalAaHistoryStorage::Compact",
        "compact history presentation sharpening");
    ExpectAbsent(
        temporalPresentationSharpen,
        "m_Cmaa2Pass",
        "compact history sharpening independent of CMAA2 allocation");
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

    const std::string_view environment = ExtractSection(
        source,
        "const bool skyOpen = DrawCollapsingHeader(",
        "const auto& lights = m_app->GetEditableLights();",
        "Environment drawer");
    ExpectContains(
        environment,
        "ImGui::TextUnformatted(\"Environment\")",
        "imported environment source label");
    ExpectContains(
        environment,
        "\"##SkyEnvironment\"",
        "imported environment source selector");
    ExpectContains(
        environment,
        "ImageBasedLightingSource::Kloppenheim03Day",
        "calibrated default environment source");
    ExpectContains(
        environment,
        "DrawDeferredDropdownOption(",
        "deferred environment source selection");
    const std::string_view environmentSourceControl = ExtractSection(
        environment,
        "const ImageBasedLightingSourceInfo&\n"
        "                selectedEnvironmentInfo =",
        "const float defaultEnvironmentExposure =",
        "environment source control");
    ExpectContains(
        environmentSourceControl,
        "m_ui.EnvironmentSource = source;",
        "environment source deferred commit");
    ExpectContains(
        environmentSourceControl,
        "m_ui.EnvironmentExposureStops =\n"
        "                                selectedInfo.defaultExposureStops;",
        "source-owned environment exposure");
    ExpectContains(
        environmentSourceControl,
        "m_app->ResetImageBasedLightingHistory(true);",
        "environment source history reset");
    const std::string_view environmentSourceReset = ExtractSection(
        environment,
        "if (DrawPresetResetIcon(\n"
        "                    \"Environment Source\",",
        "const float defaultEnvironmentExposure =",
        "environment source reset");
    ExpectContains(
        environmentSourceReset,
        "QueueDeferredControlUiAction(",
        "deferred environment source reset");
    ExpectContains(
        environmentSourceReset,
        "m_ui.EnvironmentSource = DefaultSource;",
        "default environment source reset");
    ExpectContains(
        environmentSourceReset,
        "GetImageBasedLightingSourceInfo(\n"
        "                                DefaultSource).defaultExposureStops;",
        "default environment exposure reset");
    ExpectOrdered(
        environmentSourceReset,
        "QueueDeferredControlUiAction(",
        "m_ui.EnvironmentSource = DefaultSource;",
        "deferred environment source reset");

    const std::string_view environmentExposureControl = ExtractSection(
        environment,
        "const float defaultEnvironmentExposure =",
        "if (ImGui::Checkbox(\n"
        "                    \"Ambient Fill\",",
        "environment exposure control");
    ExpectContains(
        environmentExposureControl,
        "\"Exposure##ImageBasedLighting\"",
        "environment exposure control");
    ExpectContains(
        environmentExposureControl,
        "\"%+.2f EV\"",
        "environment exposure presentation");
    ExpectContains(
        environmentExposureControl,
        "\"Environment Exposure\"",
        "environment exposure reset");
    ExpectContains(
        environmentExposureControl,
        "m_ui.EnvironmentExposureStops =\n"
        "                    defaultEnvironmentExposure;",
        "source-owned environment exposure reset");
    ExpectContains(
        environmentExposureControl,
        "m_app->ResetImageBasedLightingHistory(true);",
        "environment exposure history reset");

    const std::string_view ambientFillControls = ExtractSection(
        environment,
        "if (ImGui::Checkbox(\n"
        "                    \"Ambient Fill\",",
        "if (ImGui::Checkbox(\n"
        "                    \"Show Environment Background\",",
        "ambient fill controls");
    ExpectContains(
        ambientFillControls,
        "&m_ui.EnableAmbientFill",
        "ambient fill enable control");
    ExpectContains(
        ambientFillControls,
        "\"##AmbientFillControls\"",
        "animated ambient fill child controls");
    ExpectContains(
        ambientFillControls,
        "\"Ambient Fill Enabled\"",
        "ambient fill reset");
    ExpectContains(
        ambientFillControls,
        "m_ui.EnableAmbientFill = true;",
        "default ambient fill reset");
    ExpectContains(
        ambientFillControls,
        "m_app->ResetImageBasedLightingHistory(true);",
        "ambient fill history reset");
    ExpectContains(
        ambientFillControls,
        "\"Diffuse IBL\"",
        "ambient fill diffuse child");
    ExpectContains(
        ambientFillControls,
        "\"Specular IBL\"",
        "ambient fill specular child");
    ExpectAbsent(
        ambientFillControls,
        "\"Show Environment Background\"",
        "ambient fill independent environment background");

    const std::string_view diffuseIblControls = ExtractSection(
        environment,
        "if (ImGui::Checkbox(\n"
        "                        \"Diffuse IBL\",",
        "if (ImGui::Checkbox(\n"
        "                        \"Specular IBL\",",
        "diffuse IBL controls");
    ExpectContains(
        diffuseIblControls,
        "&m_ui.EnableDiffuseIbl",
        "diffuse IBL enable control");
    ExpectContains(
        diffuseIblControls,
        "\"##DiffuseIblControls\"",
        "animated diffuse IBL controls");
    ExpectContains(
        diffuseIblControls,
        "&m_ui.DiffuseIblStrength",
        "diffuse IBL strength control");
    ExpectContains(
        diffuseIblControls,
        "\"Diffuse IBL Enabled\"",
        "diffuse IBL enable reset");
    ExpectContains(
        diffuseIblControls,
        "\"Diffuse IBL Strength\"",
        "diffuse IBL strength reset");
    ExpectContains(
        diffuseIblControls,
        "m_ui.EnableDiffuseIbl = true;",
        "default diffuse IBL enable reset");
    ExpectContains(
        diffuseIblControls,
        "m_ui.DiffuseIblStrength = 1.f;",
        "default diffuse IBL strength reset");
    ExpectContains(
        diffuseIblControls,
        "m_app->ResetImageBasedLightingHistory(true);",
        "diffuse IBL history reset");

    const std::string_view specularIblControls = ExtractSection(
        environment,
        "if (ImGui::Checkbox(\n"
        "                        \"Specular IBL\",",
        "if (ImGui::Checkbox(\n"
        "                    \"Show Environment Background\",",
        "specular IBL controls");
    ExpectContains(
        specularIblControls,
        "&m_ui.EnableSpecularIbl",
        "specular IBL enable control");
    ExpectContains(
        specularIblControls,
        "\"##SpecularIblControls\"",
        "animated specular IBL controls");
    ExpectContains(
        specularIblControls,
        "&m_ui.SpecularIblStrength",
        "specular IBL strength control");
    ExpectContains(
        specularIblControls,
        "\"Specular IBL Enabled\"",
        "specular IBL enable reset");
    ExpectContains(
        specularIblControls,
        "\"Specular IBL Strength\"",
        "specular IBL strength reset");
    ExpectContains(
        specularIblControls,
        "m_ui.EnableSpecularIbl = true;",
        "default specular IBL enable reset");
    ExpectContains(
        specularIblControls,
        "m_ui.SpecularIblStrength = 1.f;",
        "default specular IBL strength reset");
    ExpectContains(
        specularIblControls,
        "m_app->ResetImageBasedLightingHistory(false);",
        "specular IBL history reset");

    const std::string_view environmentBackgroundControls = ExtractSection(
        environment,
        "if (ImGui::Checkbox(\n"
        "                    \"Show Environment Background\",",
        "EndDrawerBody();",
        "environment background controls");
    ExpectContains(
        environmentBackgroundControls,
        "&m_ui.ShowEnvironmentBackground",
        "environment background enable control");
    ExpectContains(
        environmentBackgroundControls,
        "\"Environment Background Enabled\"",
        "environment background reset");
    ExpectContains(
        environmentBackgroundControls,
        "m_ui.ShowEnvironmentBackground = true;",
        "default environment background reset");
    ExpectContains(
        environmentBackgroundControls,
        "m_app->ResetImageBasedLightingHistory(false);",
        "environment background history reset");
    ExpectAbsent(
        environment,
        "\"Sky Brightness\"",
        "removed procedural Sky Brightness control");

    const std::string_view lights = ExtractSection(
        source,
        "const bool lightsOpen = DrawCollapsingHeader(",
        "const bool shadowsOpen = DrawCollapsingHeader(",
        "Lights drawer");
    const std::string_view shadows = ExtractSection(
        source,
        "const bool shadowsOpen = DrawCollapsingHeader(",
        "constexpr float ActionButtonCount = 4.f;",
        "Shadows drawer");
    ExpectContains(
        source,
        "const auto& lights = m_app->GetEditableLights();",
        "editable scene and flashlight controls");
    ExpectContains(
        lights,
        "DrawDeferredDropdownOption(",
        "Lights deferred dropdowns");
    ExpectContains(
        source,
        "m_app->GetPrimaryDirectionalLight()",
        "primary sun selection");
    ExpectContains(
        lights,
        "m_SelectedLight != defaultSelectedLight",
        "primary sun reset");
    const std::string_view flashlightControls = ExtractSection(
        lights,
        "if (m_app->IsFlashlight(m_SelectedLight))",
        "const auto selectedLightIterator = std::find(",
        "selected flashlight controls");
    ExpectContains(
        flashlightControls,
        "m_ui.FlashlightEnabled",
        "selected flashlight enabled control");
    ExpectContains(
        flashlightControls,
        "Enable PBR to use the flashlight.",
        "selected flashlight PBR availability message");
    ExpectContains(
        flashlightControls,
        "##RealisticFlashlightControls",
        "animated realistic flashlight controls");
    for (const std::pair<std::string_view, std::string_view>& orderedControls : {
            std::pair<std::string_view, std::string_view>{
                "Enabled (F)", "Cast Shadows"},
            { "Cast Shadows", "Realistic Flashlight" },
            { "Realistic Flashlight", "Hotspot Size" },
            { "Hotspot Size", "Hotspot Strength" },
            { "Hotspot Strength", "Sway" },
            { "Sway", "Aim Correction" },
            { "Aim Correction", "Brightness" },
            { "Brightness", "Beam Size" },
            { "Beam Size", "Beam Roundness" },
            { "Beam Roundness", "Edge Softness" },
            { "Edge Softness", "Range" },
            { "Range", "Color" },
            { "Color", "Camera Offset"} })
    {
        ExpectOrdered(
            flashlightControls,
            orderedControls.first,
            orderedControls.second,
            "selected flashlight control order");
    }
    ExpectAbsent(
        flashlightControls,
        "Azimuth",
        "generic light controls must not leak into flashlight controls");
    const std::string compactShadows = RemoveAsciiWhitespace(shadows);
    for (const std::pair<std::string_view, std::string_view>& shadowControl : {
            std::pair<std::string_view, std::string_view>{
                "\"Screen-Space Directional Shadows##Shadows\"",
                "m_ui.ScreenSpaceDirectionalShadows"},
            {
                "\"Sparse Virtual Shadow Maps##Shadows\"",
                "m_ui.SparseVirtualShadowMaps"},
            {
                "\"Diagnostic Cascaded Shadow Maps##Shadows\"",
                "m_ui.DiagnosticCascadedShadowMaps"} })
    {
        ExpectContains(
            shadows,
            shadowControl.first,
            "integrated directional shadow control");
        ExpectContains(
            shadows,
            shadowControl.second,
            "integrated directional shadow settings");
        const std::string defaultOpenCall =
            "BeginAnimatedTreeNode(" +
            std::string(shadowControl.first) +
            ",ImGuiTreeNodeFlags_DefaultOpen)";
        ExpectContains(
            compactShadows,
            RemoveAsciiWhitespace(defaultOpenCall),
            "default-open directional shadow section");
    }
    ExpectContains(
        shadows,
        "\"##ScreenSpaceShadowControls\"",
        "animated screen-space shadow controls");
    ExpectContains(
        shadows,
        "\"SVSM Enabled\"",
        "SVSM default reset");
    ExpectContains(
        shadows,
        "DrawSvsmSettingsSurface(",
        "integrated SVSM settings surface");
    ExpectContains(
        shadows,
        "\"##DiagnosticCsmControls\"",
        "animated diagnostic CSM controls");
    ExpectContains(
        shadows,
        "\"Diagnostic CSM Enabled\"",
        "diagnostic CSM default reset");
    ExpectContains(
        shadows,
        "\"Developer Options##DiagnosticCsm\"",
        "diagnostic CSM developer controls");
    ExpectAbsent(
        shadows,
        "ImGui::TreeNodeEx(",
        "integrated directional shadow animation");
    ExpectAbsent(
        lights,
        "Screen-Space Directional Shadows##",
        "directional shadow controls must be a Lights sibling");

    const std::string_view svsmSettingsSurface = ExtractSection(
        source,
        "static bool DrawSvsmSettingsSurface(",
        "static bool DrawCenteredActionButton(",
        "SVSM settings surface");
    ExpectContains(
        svsmSettingsSurface,
        "fixed schema editable across Mode and policy changes",
        "editable requested SVSM settings");
    for (const std::string_view helper : {
            std::string_view("const auto drawCheckbox = [&]("),
            std::string_view("const auto drawCombo = [&]("),
            std::string_view("const auto drawFloat = [&](") })
    {
        ExpectContains(
            svsmSettingsSurface,
            helper,
            "fixed editable requested-settings helper");
    }
    ExpectContains(
        svsmSettingsSurface,
        "(void)available;",
        "stored requested-settings availability handling");
    ExpectAbsent(
        svsmSettingsSurface,
        "ImGui::BeginDisabled(",
        "fixed editable requested-settings surface");

    const std::string_view svsmComboHelper = ExtractSection(
        svsmSettingsSurface,
        "const auto drawCombo = [&](",
        "const auto drawFloat = [&](",
        "deferred bounded SVSM combo helper");
    ExpectContains(
        svsmComboHelper,
        "currentIndex = std::clamp(",
        "bounded SVSM combo presentation");
    ExpectContains(
        svsmComboHelper,
        "defaultIndex = std::clamp(",
        "bounded SVSM combo reset");
    ExpectContains(
        svsmComboHelper,
        "DrawDeferredDropdownOption(",
        "deferred SVSM combo selection");
    ExpectContains(
        svsmComboHelper,
        "QueueDeferredControlUiAction(",
        "deferred SVSM combo reset");
    ExpectContains(
        svsmComboHelper,
        "bool nestedResetPlacement",
        "explicit SVSM dropdown reset placement");
    ExpectContains(
        svsmComboHelper,
        "? DrawNestedDropdownResetIcon(",
        "nested SVSM dropdown reset placement");
    ExpectContains(
        svsmComboHelper,
        ": DrawPresetResetIcon(",
        "top-level SVSM dropdown reset placement");
    const auto expectSvsmComboResetPlacement = [&](
        std::string_view label,
        bool nested)
    {
        const size_t labelPosition = svsmSettingsSurface.find(label);
        const size_t applyPosition =
            labelPosition == std::string_view::npos
            ? std::string_view::npos
            : svsmSettingsSurface.find(
                  "[settings = &shadows]",
                  labelPosition + label.size());
        if (labelPosition == std::string_view::npos ||
            applyPosition == std::string_view::npos)
        {
            Fail(
                "could not locate the SVSM combo placement contract for '" +
                std::string(label) + "'.");
            return;
        }
        const std::string_view arguments = svsmSettingsSurface.substr(
            labelPosition,
            applyPosition - labelPosition);
        const size_t trailingPosition = arguments.rfind("false,");
        const size_t nestedPosition = arguments.rfind("true,");
        const bool resolvesNested =
            nestedPosition != std::string_view::npos &&
            (trailingPosition == std::string_view::npos ||
                nestedPosition > trailingPosition);
        if (resolvesNested != nested)
        {
            Fail(
                "SVSM combo '" + std::string(label) +
                "' has the wrong explicit reset placement.");
        }
    };
    for (const std::string_view label : {
             std::string_view("\"Profile##SparseVirtualShadowMaps\""),
             std::string_view("\"Filter Kernel\""),
             std::string_view("\"Filter Taps\""),
             std::string_view("\"Resolution Bias\"") })
    {
        expectSvsmComboResetPlacement(label, false);
    }
    for (const std::string_view label : {
             std::string_view("\"Mode##SparseVirtualShadowMaps\""),
             std::string_view("\"Page Marking\""),
             std::string_view("\"Moving-Light Resolution Bias\""),
             std::string_view("\"Object Invalidation Mode\""),
             std::string_view("\"Poisson Ordering\""),
             std::string_view("\"Filtering\""),
             std::string_view("\"Debug View##SparseVirtualShadowMaps\"") })
    {
        expectSvsmComboResetPlacement(label, true);
    }
    ExpectContains(
        svsmSettingsSurface,
        "shadows.dirtyPageScatterAmplificationGuardEnabled =\n"
        "                        shadows.dirtyPageScatterRasterEnabled;",
        "Dirty Page Scatter composite owner mutation");
    ExpectContains(
        svsmSettingsSurface,
        ".dirtyPageScatterAmplificationGuardEnabled !=\n"
        "                                presetDefaults\n"
        "                                    "
        ".dirtyPageScatterAmplificationGuardEnabled",
        "Dirty Page Scatter composite reset visibility");

    const std::string_view svsmDiagnostics = ExtractSection(
        svsmSettingsSurface,
        "if (BeginAnimatedTreeNode(\n"
        "                \"Diagnostics##SparseVirtualShadowMaps\",",
        "if (customChanged || resetApplied)",
        "SVSM diagnostics controls");
    ExpectContains(
        svsmDiagnostics,
        "\"Detailed GPU Stage Timing\"",
        "SVSM detailed GPU stage timing");
    ExpectContains(
        svsmDiagnostics,
        "\"Debug View##SparseVirtualShadowMaps\"",
        "SVSM debug view");
    ExpectContains(
        svsmDiagnostics,
        "int(std::size(DebugLabels))",
        "bounded SVSM debug view");
    ExpectContains(
        svsmDiagnostics,
        "SvsmDebugView(selectedDebugView)",
        "typed deferred SVSM debug view");

    for (const std::pair<std::string_view, std::string_view>& region : {
            std::pair<std::string_view, std::string_view>{
                "\"##SvsmFinitePageRenderBudget\"",
                "\"Page Render Budget\""},
            {
                "\"##SvsmReceiverDistanceClampTuning\"",
                "\"Distance Clamp Start x Extent\""},
            {
                "\"##SvsmStaticDepthHierarchyTuning\"",
                "\"Static HZB Conservative Bias\""},
            {
                "\"##SvsmDirtyPageScatterTuning\"",
                "\"Scatter Maximum Page Amplification\""},
            {
                "\"##SvsmLightDepthGuardBandTuning\"",
                "\"Light-Depth Guard Fraction\""} })
    {
        const size_t regionBegin =
            svsmSettingsSurface.find(region.first);
        const size_t regionControl =
            regionBegin == std::string_view::npos
                ? std::string_view::npos
                : svsmSettingsSurface.find(
                    region.second,
                    regionBegin + region.first.size());
        const size_t regionEnd =
            regionBegin == std::string_view::npos
                ? std::string_view::npos
                : svsmSettingsSurface.find(
                    "EndAnimatedToggleRegion();",
                    regionBegin + region.first.size());
        if (regionBegin == std::string_view::npos ||
            regionControl == std::string_view::npos ||
            regionEnd == std::string_view::npos ||
            regionControl >= regionEnd)
        {
            Fail(
                "SVSM conditional tuning must place '" +
                std::string(region.second) +
                "' inside the balanced animated region '" +
                std::string(region.first) + "'.");
        }
    }

    const std::string_view screenSpaceShadowControls = ExtractSection(
        shadows,
        "\"Screen-Space Directional Shadows##Shadows\"",
        "\"Sparse Virtual Shadow Maps##Shadows\"",
        "screen-space shadow controls");
    const std::string compactScreenSpaceShadowControls =
        RemoveAsciiWhitespace(screenSpaceShadowControls);
    for (const std::string_view resetCall : {
            std::string_view(
                "DrawNestedDropdownResetIcon(\"ScreenSpaceShadowProfile\","),
            std::string_view(
                "DrawNestedDropdownResetIcon(\"ScreenSpaceShadowLength\","),
            std::string_view(
                "DrawNestedDropdownResetIcon(\"ScreenSpaceShadowHardSamples\","),
            std::string_view(
                "DrawNestedDropdownResetIcon(\"ScreenSpaceShadowFade-OutSamples\","),
            std::string_view(
                "DrawNestedDropdownResetIcon(\"ScreenSpaceShadowDebugView\","),
            std::string_view(
                "DrawPresetResetIcon(\"ScreenSpaceShadowsEnabled\","),
            std::string_view(
                "DrawPresetResetIcon(\"ScreenSpaceShadowSurfaceThickness\","),
            std::string_view(
                "DrawPresetResetIcon(\"ScreenSpaceShadowBilinearThreshold\","),
            std::string_view(
                "DrawPresetResetIcon(\"ScreenSpaceShadowContrast\","),
            std::string_view(
                "DrawPresetResetIcon(\"ScreenSpaceShadowIgnoreEdgePixels\","),
            std::string_view(
                "DrawPresetResetIcon(\"ScreenSpaceShadowPrecisionOffset\","),
            std::string_view(
                "DrawPresetResetIcon(\"ScreenSpaceShadowBilinearOffsetMode\","),
            std::string_view(
                "DrawPresetResetIcon(\"ScreenSpaceShadowEarlyOut\",") })
    {
        ExpectContains(
            compactScreenSpaceShadowControls,
            resetCall,
            "screen-space shadow reset coverage");
    }
    for (const std::string_view tooltip : {
            std::string_view("Trade trace reach and cost"),
            std::string_view("Set the runtime screen-space trace reach."),
            std::string_view("Set nonlinear-depth occluder thickness."),
            std::string_view("Set the relative depth discontinuity"),
            std::string_view("Set visibility transition contrast."),
            std::string_view("fully hard contact"),
            std::string_view("samples that soften"),
            std::string_view("Prevent detected depth-edge pixels"),
            std::string_view("toward the near plane"),
            std::string_view("one pixel farther"),
            std::string_view("Keep tracing to preserve complete debug"),
            std::string_view("Show raw occlusion") })
    {
        ExpectContains(
            screenSpaceShadowControls,
            tooltip,
            "screen-space shadow tooltip coverage");
    }

    const std::string_view diagnosticCsmControls = ExtractSection(
        shadows,
        "\"Diagnostic Cascaded Shadow Maps##Shadows\"",
        "EndDrawerBody();",
        "diagnostic CSM controls");
    const std::string compactDiagnosticCsmControls =
        RemoveAsciiWhitespace(diagnosticCsmControls);
    for (const std::string_view section : {
            std::string_view("\"ProjectionandBias##DiagnosticCsm\""),
            std::string_view("\"CacheUpdatePolicy##DiagnosticCsm\""),
            std::string_view("\"CullingandRaster##DiagnosticCsm\""),
            std::string_view("\"Unabstracted##DiagnosticCsm\""),
            std::string_view("\"Diagnostics##DiagnosticCsm\"") })
    {
        ExpectContains(
            compactDiagnosticCsmControls,
            section,
            "full diagnostic CSM section coverage");
    }
    for (const std::string_view helper : {
            std::string_view("constautodrawCsmCheckbox="),
            std::string_view("constautodrawCsmFloat="),
            std::string_view("constautodrawCsmUint=") })
    {
        ExpectContains(
            compactDiagnosticCsmControls,
            helper,
            "diagnostic CSM tooltip and reset helper");
    }
    ExpectContains(
        diagnosticCsmControls,
        "ImGui::SetItemTooltip(\"%s\", tooltip);",
        "diagnostic CSM per-control tooltip");
    ExpectContains(
        diagnosticCsmControls,
        "DrawPresetResetIcon(",
        "diagnostic CSM per-control reset");

    constexpr const char* diagnosticCsmFieldContracts[] = {
        "shadows.cascadeCount",
        "shadows.shadowMapResolution",
        "shadows.maximumShadowDistance",
        "shadows.maximumLightDepth",
        "shadows.cascadeDistributionExponent",
        "shadows.cascadeTransitionFraction",
        "shadows.shadowDistanceFadeoutFraction",
        "shadows.projectionSnapTexelMultiple",
        "shadows.enforceUeMinimumLightDepth",
        "shadows.depthBias",
        "shadows.slopeScaledDepthBias",
        "shadows.directionalLightShadowBias",
        "shadows.directionalLightShadowSlopeBias",
        "shadows.receiverDepthBias",
        "shadows.filter",
        "shadows.poissonTapCount",
        "shadows.filterRadiusTexels",
        "shadows.use16BitDepthEnabled",
        "shadows.opaqueDepthStateMergingEnabled",
        "shadows.positionOnlyOpaqueEnabled",
        "shadows.translationOnlyCasterTransformEnabled",
        "shadows.inputAssemblerCasterFetchEnabled",
        "shadows.precomputedDepthAxisInverseLengthEnabled",
        "shadows.conservativeSaturatedSlopeEnabled",
        "shadows.algebraicSlowSlopeEnabled",
        "shadows.preNormalizedReceiverLightDirectionEnabled",
        "shadows.precomposedClipToShadowEnabled",
        "shadows.accurateCasterCullingEnabled",
        "shadows.ueCasterRadiusThresholdEnabled",
        "shadows.casterRadiusThreshold",
        "shadows.singleTraversalCasterClassificationEnabled",
        "shadows.precomputedReceiverHullAxesEnabled",
        "shadows.sharedCasterLightProjectionEnabled",
        "shadows.directCasterSubmissionEnabled",
        "shadows.cachedShadowDrawListsEnabled",
        "shadows.batchedFullRedrawClearEnabled",
        "shadows.receiverRasterScissorEnabled",
        "shadows.wholeMapReuseEnabled",
        "shadows.wholeCascadeReuseEnabled",
        "shadows.dirtyRectanglesEnabled",
        "shadows.scrollingEnabled",
        "shadows.minimumScrollOverlap",
        "shadows.detailedGpuTimingEnabled",
        "shadows.debugView"
    };
    static_assert(
        sizeof(diagnosticCsmFieldContracts) /
                sizeof(diagnosticCsmFieldContracts[0]) ==
            44u,
        "diagnostic CSM UI contract must cover all 44 timing fields");
    for (const char* field : diagnosticCsmFieldContracts)
    {
        ExpectContains(
            compactDiagnosticCsmControls,
            field,
            "diagnostic CSM 44-field coverage");
    }

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
        "if (crosshairOpacity > 0.f)",
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
        "constexpr float UiPanelShadowBlurPixels = 10.f;",
        "shared zoom and panel shadow blur");
    ExpectContains(
        source,
        "constexpr float UiPanelShadowOpacity = 0.34f;",
        "shared zoom and panel shadow opacity");
    ExpectContains(
        source,
        "constexpr float UiPanelShadowOffsetYPixels = 3.f;",
        "shared zoom and panel shadow offset");
    ExpectContains(
        source,
        "constants.shadowBlur = UiPanelShadowBlurPixels;",
        "pixel zoom shared shadow blur");
    ExpectContains(
        source,
        "constants.shadowOpacity = UiPanelShadowOpacity;",
        "pixel zoom shared shadow opacity");
    ExpectContains(
        source,
        "constants.shadowOffsetY = UiPanelShadowOffsetYPixels;",
        "pixel zoom shared shadow offset transport");
    ExpectContains(
        source,
        "static_assert(sizeof(PixelZoomConstants) == 96u);",
        "pixel zoom constant-buffer packing guard");
    ExpectContains(
        pixelZoomShader,
        "g_PixelZoom.shadowOffsetY",
        "pixel zoom shader shared shadow offset");
    const std::string_view pixelZoomComposite = ExtractSection(
        source,
        "            m_PixelZoomPass->Composite(",
        "        imgui_nvrhi->render(framebuffer);",
        "pixel zoom skin composition");
    ExpectContains(
        pixelZoomComposite,
        "ImGui::GetStyle().WindowRounding",
        "skin-derived pixel zoom rounding");
    ExpectAbsent(
        pixelZoomComposite,
        "                8.f,",
        "removed hard-coded Amp rounding from OG zoom");
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
    const std::string compactVisibility =
        RemoveAsciiWhitespace(visibility);
    const std::string compactAliasing =
        RemoveAsciiWhitespace(aliasing);
    const std::string compactSvsmSettingsSurface =
        RemoveAsciiWhitespace(svsmSettingsSurface);
    constexpr const char* visibilityNestedResetCalls[] = {
        "DrawNestedDropdownResetIcon(\"VisibilityEstimator\",",
        "DrawNestedDropdownResetIcon(\"VisibilityNoisePattern\",",
        "DrawNestedDropdownResetIcon(\"VisibilityReconstructionMethod\",",
        "DrawNestedDropdownResetIcon(\"VisibilityFinalApplication\","
    };
    for (const char* nestedResetCall : visibilityNestedResetCalls)
    {
        ExpectContains(
            compactVisibility,
            nestedResetCall,
            "Visibility nested dropdown reset scope");
    }
    constexpr const char* aliasingNestedResetCalls[] = {
        "DrawNestedDropdownResetIcon(\"AliasingMorphology\",",
        "DrawNestedDropdownResetIcon(label,"
    };
    for (const char* nestedResetCall : aliasingNestedResetCalls)
    {
        ExpectContains(
            compactAliasing,
            nestedResetCall,
            "Aliasing nested dropdown reset scope");
    }
    ExpectContains(
        compactSvsmSettingsSurface,
        "DrawNestedDropdownResetIcon(label,",
        "SVSM nested dropdown reset scope");
    constexpr const char* screenSpaceShadowNestedResetCalls[] = {
        "DrawNestedDropdownResetIcon(\"ScreenSpaceShadowProfile\",",
        "DrawNestedDropdownResetIcon(\"ScreenSpaceShadowLength\",",
        "DrawNestedDropdownResetIcon(\"ScreenSpaceShadowHardSamples\",",
        "DrawNestedDropdownResetIcon(\"ScreenSpaceShadowFade-OutSamples\",",
        "DrawNestedDropdownResetIcon(\"ScreenSpaceShadowDebugView\","
    };
    for (const char* nestedResetCall :
        screenSpaceShadowNestedResetCalls)
    {
        ExpectContains(
            compactScreenSpaceShadowControls,
            nestedResetCall,
            "screen-space shadow nested dropdown reset scope");
    }
    constexpr const char* diagnosticCsmNestedResetCalls[] = {
        "DrawNestedDropdownResetIcon(\"DiagnosticCSMProfile\",",
        "DrawNestedDropdownResetIcon(\"DiagnosticCSMFilter\",",
        "DrawNestedDropdownResetIcon(\"DiagnosticCSMFilterTaps\",",
        "DrawNestedDropdownResetIcon(\"DiagnosticCSMDebugView\","
    };
    for (const char* nestedResetCall : diagnosticCsmNestedResetCalls)
    {
        ExpectContains(
            compactDiagnosticCsmControls,
            nestedResetCall,
            "diagnostic CSM nested dropdown reset scope");
    }

    struct NestedResetScope
    {
        std::string_view source;
        size_t expectedOccurrences;
        const char* contract;
    };
    const NestedResetScope nestedResetScopes[] = {
        {
            resetPlacementHelpers,
            1u,
            "nested dropdown reset helper definition"},
        {
            visibility,
            4u,
            "Visibility nested dropdown resets"},
        {
            aliasing,
            2u,
            "Aliasing nested dropdown resets"},
        {
            svsmSettingsSurface,
            1u,
            "SVSM nested dropdown reset wrapper"},
        {
            screenSpaceShadowControls,
            5u,
            "screen-space shadow nested dropdown resets"},
        {
            diagnosticCsmControls,
            4u,
            "diagnostic CSM nested dropdown resets"}
    };
    constexpr std::string_view NestedResetCall =
        "DrawNestedDropdownResetIcon(";
    for (const NestedResetScope& scope : nestedResetScopes)
    {
        const size_t occurrences =
            CountOccurrences(scope.source, NestedResetCall);
        if (occurrences != scope.expectedOccurrences)
        {
            Fail(
                std::string(scope.contract) + " must contain exactly " +
                std::to_string(scope.expectedOccurrences) +
                " explicitly allowlisted occurrence(s).");
        }
    }
    size_t nestedResetPosition = 0u;
    while ((nestedResetPosition =
                source.find(NestedResetCall, nestedResetPosition)) !=
        std::string_view::npos)
    {
        bool inAllowedScope = false;
        for (const NestedResetScope& scope : nestedResetScopes)
        {
            if (scope.source.empty())
                continue;
            const size_t scopeBegin =
                size_t(scope.source.data() - source.data());
            const size_t scopeEnd = scopeBegin + scope.source.size();
            if (nestedResetPosition >= scopeBegin &&
                nestedResetPosition < scopeEnd)
            {
                inAllowedScope = true;
                break;
            }
        }
        if (!inAllowedScope)
        {
            Fail(
                "nested dropdown reset call appears outside every explicit "
                "Settings scope.");
        }
        nestedResetPosition += NestedResetCall.size();
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
        "ImGui::PopItemWidth();",
        "nested control-width lifetime balance");
    ExpectOrdered(
        animatedNestedSection,
        "ImGui::PopItemWidth();",
        "ImGui::Unindent(context.indentSpacing);",
        "nested gutter lifetime balance");
    ExpectOrdered(
        animatedNestedSection,
        "const float inheritedItemWidth = ImGui::CalcItemWidth();",
        "ImGui::BeginChild(",
        "nested control-width capture");
    ExpectOrdered(
        animatedNestedSection,
        "ImGui::Indent(indentSpacing);",
        "ImGui::PushItemWidth(inheritedItemWidth);",
        "nested control-width inheritance");
    ExpectOrdered(
        animatedNestedSection,
        "ImGui::Unindent(context.indentSpacing);",
        "ImGui::EndChild();",
        "nested gutter inside the animated child");
    ExpectContains(
        animatedNestedSection,
        "ImGuiID measurementValidKey = 0;",
        "zero-height nested-tree measurement validity");
    ExpectContains(
        animatedNestedSection,
        "SubmitUiExpandedMeasurement(",
        "zero-height nested-tree measurement completion");
    const std::string_view animatedToggleSection = ExtractSection(
        source,
        "struct UiToggleRegionAnimationState",
        "static ImVec2 MovePointToward(",
        "toggle-region measurement state");
    ExpectContains(
        animatedToggleSection,
        "UiExpandedMeasurementState measurement;",
        "zero-height toggle-region measurement state");
    ExpectContains(
        animatedToggleSection,
        "NeedsInitialUiExpandedMeasurement(",
        "zero-height toggle-region initial measurement");
    ExpectContains(
        animatedToggleSection,
        "SubmitUiExpandedMeasurement(",
        "zero-height toggle-region measurement completion");
    ExpectOrdered(
        animatedToggleSection,
        "const float inheritedItemWidth = ImGui::CalcItemWidth();",
        "ImGui::BeginChild(",
        "toggle-region control-width capture");
    ExpectOrdered(
        animatedToggleSection,
        "ImGui::BeginChild(",
        "ImGui::PushItemWidth(inheritedItemWidth);",
        "toggle-region control-width inheritance");
    ExpectOrdered(
        animatedToggleSection,
        "ImGui::EndDisabled();",
        "ImGui::PopItemWidth();",
        "toggle-region control-width lifetime balance");
    ExpectOrdered(
        animatedToggleSection,
        "ImGui::PopItemWidth();",
        "ImGui::EndChild();",
        "toggle-region control-width inside child");
    ExpectAbsent(
        animatedToggleSection,
        "state.measuredHeight <= 0.f",
        "zero-height toggle-region sentinel");
    ExpectAbsent(
        source,
        "measuredHeight <= 0.f",
        "zero-height animated-region sentinel");
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
        "MONITOR_DEFAULTTONEAREST",
        "current-monitor startup placement");
    ExpectAbsent(
        source,
        "GetDpiForWindow(nativeWindow)",
        "work-area-driven startup shrink");
    ExpectAbsent(
        source,
        "maximumClientWidth",
        "work-area-driven client-width clamp");
    ExpectContains(
        source,
        "deviceParams.backBufferWidth = 1920;",
        "exact default startup width");
    ExpectContains(
        source,
        "deviceParams.backBufferHeight = 1080;",
        "exact default startup height");
    ExpectContains(
        source,
        "CenterWindowInMonitorWorkArea(",
        "work-area-centered startup placement");
    ExpectContains(
        source,
        "value & ~7",
        "divisible-by-eight startup client dimensions");
    ExpectContains(
        source,
        "SWP_NOSIZE",
        "move-only final startup centering");
    ExpectContains(
        source,
        "!benchmarkCameraRequested",
        "exact-size benchmark placement bypass");
    ExpectContains(
        source,
        "constexpr float SettingsWindowWidthInFontHeights = 29.3f;",
        "red-line-aligned constant Settings window width");
    ExpectAbsent(
        source,
        "const float statusContentWidth =",
        "live-status-driven Settings window width");
    ExpectContains(
        source,
        "std::array<std::string, 6> m_PerformanceStatValues;",
        "triangle performance-stat slot");
    const std::string_view performanceLine = ExtractSection(
        source,
        "static std::string BuildPerformanceLine(",
        "static double StepTowardByTenth(",
        "performance status line");
    ExpectOrdered(
        performanceLine,
        "values[0]",
        "values[5]",
        "triangle counter after resolution");
    ExpectOrdered(
        performanceLine,
        "values[5]",
        "values[3]",
        "triangle counter before bandwidth");
    const std::string_view ogPerformanceLines = ExtractSection(
        source,
        "static std::array<std::string, 2> BuildOgPerformanceLines(",
        "static double StepTowardByTenth(",
        "two-row OG performance status");
    ExpectOrdered(
        ogPerformanceLines,
        "values[0]",
        "values[5]",
        "OG resolution before triangle count");
    ExpectOrdered(
        ogPerformanceLines,
        "values[5]",
        "values[3]",
        "OG triangle count before bandwidth");
    ExpectOrdered(
        ogPerformanceLines,
        "values[3]",
        "values[4]",
        "OG first row before second row");
    ExpectOrdered(
        ogPerformanceLines,
        "values[4]",
        "values[1]",
        "OG compute before frame time");
    ExpectOrdered(
        ogPerformanceLines,
        "values[1]",
        "values[2]",
        "OG frame time before frame rate");
    ExpectContains(
        source,
        "const bool splitOgPerformanceStatus =\n"
        "            hasPerformanceStatus &&\n"
        "            m_ComposedUiSkin == UiSkin::Og;",
        "OG-only performance status split");
    ExpectContains(
        source,
        "splitOgPerformanceStatus\n"
        "                        ? SettingsStatusLineSpacing + fontSize",
        "OG second-row collapsed height");
    ExpectContains(
        source,
        "ogPerformanceLines[0].c_str()",
        "OG first performance row");
    ExpectContains(
        source,
        "ogPerformanceLines[1].c_str()",
        "OG second performance row");
    ExpectContains(
        source,
        "else\n"
        "            {\n"
        "                ImGui::TextUnformatted(performanceLine.c_str());",
        "unchanged single-row Amp performance status");
    const std::string_view sharedSurfaceBackground = ExtractSection(
        source,
        "    static void PushPanelBodySurface()",
        "    inline static constexpr float",
        "shared panel-body surface helper");
    ExpectContains(
        sharedSurfaceBackground,
        "ImGuiCol_WindowBg,\n"
        "            g_UiVisualTokens.panelBodySurface",
        "shared full-RGBA panel-body surface");
    ExpectContains(
        source,
        "colors[ImGuiCol_WindowBg] =\n"
        "                ImVec4(0.018f, 0.018f, 0.018f, 0.60f);",
        "neutral Amp root surface RGB");
    ExpectContains(
        source,
        "colors[ImGuiCol_FrameBg] =\n"
        "                ImVec4(0.018f, 0.018f, 0.018f, 0.72f);",
        "neutral Amp control surface RGB");
    ExpectContains(
        source,
        "colors[ImGuiCol_Button] =\n"
        "                ImVec4(0.018f, 0.018f, 0.018f, 0.72f);",
        "neutral Amp button surface RGB");
    ExpectContains(
        source,
        "tokens.panelBodySurface =\n"
        "            colors[ImGuiCol_WindowBg];",
        "OG-compatible panel body source");
    ExpectContains(
        source,
        "tokens.panelBodySurface.w =\n"
        "                colors[ImGuiCol_PopupBg].w;",
        "Amp panel body opacity");
    if (CountOccurrences(
            source,
            "        PushPanelBodySurface();") != 3u)
    {
        Fail("Settings, Materials, and the command interface must each push "
            "the one shared full-RGBA panel body surface.");
    }
    ExpectAbsent(
        source,
        "GetCommandSurfaceBackgroundAlpha",
        "retired alpha-only panel-surface contract");
    ExpectContains(
        commandInterface,
        "PushPanelBodySurface();",
        "command-interface full-RGBA panel surface");
    ExpectAbsent(
        commandInterface,
        "ImGui::SetNextWindowBgAlpha(",
        "command-interface alpha-only panel surface");
    const std::string_view settingsWindowBackground = ExtractSection(
        source,
        "        // WindowBg is absent beneath title bars.",
        "        ImGui::Begin(\n"
        "            \"Settings\"",
        "Settings root background policy");
    ExpectContains(
        settingsWindowBackground,
        "PushPanelBodySurface();",
        "Settings full-RGBA panel surface");
    ExpectAbsent(
        settingsWindowBackground,
        "ImGui::SetNextWindowBgAlpha(",
        "Settings alpha-only panel surface");
    ExpectContains(
        source,
        "FormatTriangleCount(snapshot.submittedTriangles)",
        "compact submitted-triangle status");
    ExpectAbsent(
        source,
        "return \"Unpacked Offline\"",
        "retired unpacked Offline noise label");
    ExpectAbsent(
        source,
        "return \"Packed Offline\"",
        "retired packed Offline noise label");
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
    const std::string_view settingsScrollCorrection = ExtractSection(
        source,
        "    static void EndSettingsScrollStability()",
        "    struct DrawerAnimationContext",
        "Settings scroll anchor correction");
    ExpectContains(
        settingsScrollCorrection,
        "window->ContentSizeExplicit.y",
        "current-frame Settings content height");
    ExpectContains(
        settingsScrollCorrection,
        "window->DC.CursorMaxPos.y",
        "current-frame Settings content extent");
    ExpectContains(
        settingsScrollCorrection,
        "window->DC.CursorStartPos.y",
        "current-frame Settings content origin");
    ExpectContains(
        settingsScrollCorrection,
        "window->InnerRect.GetHeight()",
        "current-frame Settings viewport height");
    ExpectContains(
        settingsScrollCorrection,
        "ResolveUiScrollAnchorCorrection(",
        "single-use Settings scroll correction");
    ExpectContains(
        settingsScrollCorrection,
        "window->ScrollTarget.y < FLT_MAX",
        "pending ImGui scroll-target preservation");
    ExpectContains(
        settingsScrollCorrection,
        "window->Scroll.y = correction.scrollY;",
        "direct Settings scroll correction");
    ExpectAbsent(
        settingsScrollCorrection,
        "ImGui::SetScrollY(",
        "removed delayed Settings scroll target");
    ExpectOrdered(
        settingsScrollCorrection,
        "ResolveUiScrollAnchorCorrection(",
        "window->Scroll.y = correction.scrollY;",
        "resolved Settings scroll correction assignment");
    ExpectContains(
        source,
        "context.rootDrawVertexStart =",
        "Settings content draw-range capture");
    ExpectContains(
        settingsScrollCorrection,
        "const float visualScrollDelta =",
        "same-frame Settings visual correction");
    ExpectContains(
        settingsScrollCorrection,
        "g_SettingsAppearanceDrawLists",
        "all Settings child draw-list correction");
    ExpectContains(
        settingsScrollCorrection,
        "drawList->VtxBuffer[vertexIndex].pos.y -=",
        "same-frame Settings vertex correction");
    ExpectContains(
        settingsScrollCorrection,
        "command.ClipRect.y = std::max(",
        "nested Settings clip correction");
    const std::string_view scrollCorrectionHelper = ExtractSection(
        uiAnimationSource,
        "    struct UiScrollAnchorCorrection",
        "    // Retain the Settings viewport",
        "pure Settings scroll correction");
    ExpectContains(
        scrollCorrectionHelper,
        "if (hasPendingScrollTarget ||",
        "pending ImGui scroll-target ownership");
    ExpectContains(
        scrollCorrectionHelper,
        "return { false, currentScrollY };",
        "pending ImGui scroll-target preservation");
    ExpectContains(
        scrollCorrectionHelper,
        "requestedScrollY > maximumScrollY",
        "current-frame Settings maximum clamp");
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
    ExpectContains(
        source,
        "presetLabelSettings.enabled = true;",
        "AA Developer Options inherited rows independent of execution bypass");
    ExpectContains(
        source,
        "currentLabelSettings.enabled = true;",
        "AA Developer Options retained state independent of execution bypass");
    ExpectContains(
        source,
        "m_ui.GetResolvedAntiAliasingSettings(\n"
        "                        currentLabelSettings)",
        "AA Developer Options resolve retained state while disabled");
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
        "static bool BeginRoundedCombo(",
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
        "static void ApplyWordSpacing(",
        "deferred dropdown selection wrapper");
    ExpectContains(
        deferredDropdownOption,
        "if (!activated || selected)",
        "active dropdown option no-op");
    ExpectContains(
        deferredDropdownOption,
        "QueueDeferredDropdownUiAction(",
        "deferred dropdown wrapper queue routing");

    const std::string_view sliderTrackStyle = ExtractSection(
        source,
        "static void PushPanelSliderTrackStyle()",
        "static bool DrawSliderFloat(",
        "shared slider track style");
    const std::string_view sliderFloat = ExtractSection(
        source,
        "static bool DrawSliderFloat(",
        "static bool DrawSliderInt(",
        "float slider helper");
    const std::string_view sliderInt = ExtractSection(
        source,
        "static bool DrawSliderInt(",
        "static bool DrawSvsmSettingsSurface(",
        "integer slider helper");
    ExpectAbsent(
        sliderFloat,
        "QueueDeferred",
        "float slider immediate interaction");
    ExpectAbsent(
        sliderInt,
        "QueueDeferred",
        "integer slider immediate interaction");
    if (CountOccurrences(
            sliderTrackStyle,
            "ImGui::PushStyleColor(") != 3u)
    {
        Fail("The shared slider track style must push exactly three colors.");
    }
    if (CountOccurrences(
            sliderFloat,
            "ImGui::PopStyleColor(3);") != 1u ||
        CountOccurrences(
            sliderFloat,
            "ImGui::PopStyleColor(4);") != 0u)
    {
        Fail("The float slider must pop exactly the three shared track colors.");
    }
    if (CountOccurrences(
            sliderInt,
            "ImGui::PopStyleColor(3);") != 1u ||
        CountOccurrences(
            sliderInt,
            "ImGui::PopStyleColor(4);") != 0u)
    {
        Fail("The integer slider must pop exactly the three shared track colors.");
    }

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
        "const bool materialInspectorPlacementIdle =",
        "Material Inspector placement commit gate");
    ExpectContains(
        compositionIdlePolicy,
        "m_MaterialInspectorZoomPlacement <= 0.f",
        "Material Inspector upper placement endpoint");
    ExpectContains(
        compositionIdlePolicy,
        "m_MaterialInspectorZoomPlacement >= 1.f",
        "Material Inspector lower placement endpoint");
    ExpectContains(
        compositionIdlePolicy,
        "materialInspectorPlacementIdle &&",
        "Material Inspector placement commit idleness");
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
    ExpectContains(
        settingsBody,
        "const bool settingsScrollInputBlocked =",
        "same-frame scroll correction input barrier");
    ExpectContains(
        settingsBody,
        "g_SettingsScrollStabilityContext.layoutAnimatingLastFrame",
        "continuing Settings layout-motion input barrier");
    if (CountOccurrences(
            settingsBody,
            "if (settingsScrollInputBlocked)") != 2)
    {
        Fail(
            "the Settings scroll-correction input barrier must begin and end "
            "exactly once.");
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
        "DrawMaterialInspector(",
        "FinishUnsubmittedDeferredDropdownPopupTransition();",
        "hidden Settings Material Inspector composition");
    ExpectOrdered(
        hiddenSettingsCommit,
        "FinishUnsubmittedDeferredDropdownPopupTransition();",
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
    if (CountOccurrences(buildUi, "DrawMaterialInspector(") != 2u)
    {
        Fail("fully hidden and visible Settings paths must each compose the "
            "Material Inspector exactly once.");
    }
    ExpectAbsent(
        buildUi,
        "float(width) - fontSize * 0.6f",
        "removed corner-pinned Material Inspector placement");
    ExpectAbsent(
        buildUi,
        "if (m_ui.ShowMaterialEditor)",
        "shared Material Inspector composition helper");
    const std::string_view materialEditor = ExtractSection(
        source,
        "    void DrawMaterialInspector(",
        "    static std::string BuildPerformanceLine(",
        "Material Editor deferred domain selection");
    ExpectContains(
        materialEditor,
        "ResolveMaterialInspectorLayout(",
        "fixed zoom-derived Material Inspector geometry");
    ExpectContains(
        materialEditor,
        "m_SettingsPanelMarginPixels",
        "consistent-margin Material Inspector geometry");
    ExpectContains(
        materialEditor,
        "m_MaterialInspectorZoomPlacement",
        "Material Inspector placement state");
    ExpectContains(
        materialEditor,
        "ImGui::SetNextWindowPos(",
        "fixed Material Inspector position");
    ExpectContains(
        materialEditor,
        "ImGui::SetNextWindowSizeConstraints(",
        "fixed Material Inspector width and maximum height");
    ExpectContains(
        materialEditor,
        "IsMaterialInspectorPresentationActive(",
        "retained Material Inspector close presentation");
    ExpectContains(
        materialEditor,
        "SmoothPixelZoomVisibility(\n"
        "                    m_MaterialInspectorAppearance)",
        "Amp Material Inspector fade curve");
    ExpectContains(
        materialEditor,
        "PixelZoomMinimumWindowScale +",
        "Amp Material Inspector zoom curve");
    ExpectContains(
        materialEditor,
        "materialWindowFlags |= ImGuiWindowFlags_NoInputs;",
        "noninteractive Material Inspector transitions");
    ExpectContains(
        materialEditor,
        "ImGui::Begin(\n"
        "            \"Materials\",\n"
        "            nullptr,\n"
        "            materialWindowFlags);",
        "Materials title without native close button");
    ExpectAbsent(
        materialEditor,
        "\"Materials\",\n"
        "            &m_ui.ShowMaterialEditor,",
        "removed Materials title-bar close button");
    ExpectAbsent(
        materialEditor,
        "\"Material Editor\"",
        "retired Material Editor visible title");
    ExpectAbsent(
        materialEditor,
        "ImGuiWindowFlags_NoCollapse",
        "visible Material Editor title-triangle close control");
    ExpectContains(
        materialEditor,
        "if (materialEditorWindow->WantCollapseToggle)",
        "Material Editor title-triangle close request");
    ExpectContains(
        materialEditor,
        "materialEditorWindow->WantCollapseToggle = false;",
        "consumed native Material Editor collapse request");
    ExpectContains(
        materialEditor,
        "materialEditorWindow->Collapsed = false;",
        "Material Editor native collapse suppression");
    ExpectContains(
        materialEditor,
        "m_ui.ShowMaterialEditor = false;",
        "Material Editor retained close target");
    ExpectOrdered(
        materialEditor,
        "const bool materialEditorVisible = ImGui::Begin(",
        "if (materialEditorWindow->WantCollapseToggle)",
        "same-frame Material Editor title-triangle handoff");
    ExpectOrdered(
        materialEditor,
        "materialEditorWindow->WantCollapseToggle = false;",
        "material->dirty |=",
        "full Material body submission during close handoff");
    ExpectContains(
        materialEditor,
        "if (materialEditorVisible)\n"
        "        {",
        "Material body submission from Begin result");
    ExpectAbsent(
        materialEditor,
        "if (materialEditorVisible &&",
        "Material close-target body guard");
    ExpectContains(
        imguiRuntimePolicy,
        "strcmp(window->Name, \"Materials\") != 0 && "
        "g.IO.MouseClickedCount[0] == 2",
        "Materials title double-click native-collapse suppression");
    if (CountOccurrences(
            materialEditor,
            "g_UiVisualTokens.settingsTitleSurface") != 3u)
    {
        Fail("Material Editor must reuse the shared title surface for resting, "
            "active, and collapsed title states.");
    }
    ExpectContains(
        materialEditor,
        "PushPanelBodySurface();",
        "Materials shared full-RGBA body surface");
    ExpectAbsent(
        materialEditor,
        "ImGui::SetNextWindowBgAlpha(",
        "Materials alpha-only body surface");
    ExpectContains(
        materialEditor,
        "const float materialControlWidth =\n"
        "                    ImGui::CalcItemWidth();",
        "Material Domain slider-column width");
    ExpectAbsent(
        materialEditor,
        "ImGui::SetNextItemWidth(-FLT_MIN)",
        "removed full-width Material Domain combo");
    ExpectContains(
        materialEditor,
        "ImGuiCol_ChildBg,\n"
        "                    g_UiVisualTokens.drawerBackground",
        "Materials drawer-style light body plate");
    ExpectContains(
        materialEditor,
        "ImGuiStyleVar_ChildRounding,\n"
        "                    g_UiVisualTokens.drawerRounding",
        "Materials drawer-style body rounding");
    ExpectContains(
        materialEditor,
        "\"##MaterialControlsBody\"",
        "Materials controls body child identity");
    ExpectContains(
        materialEditor,
        "ImGuiChildFlags_AlwaysUseWindowPadding |\n"
        "                            ImGuiChildFlags_AutoResizeY |\n"
        "                            ImGuiChildFlags_AlwaysAutoResize",
        "Materials auto-height drawer-style body plate");
    ExpectContains(
        materialEditor,
        "ImGui::PushItemWidth(materialControlWidth);",
        "Materials plate preserves Material Domain slider width");
    ExpectOrdered(
        materialEditor,
        "ImGui::PopID();",
        "\"##MaterialControlsBody\"",
        "Materials plate after Material Domain");
    ExpectOrdered(
        materialEditor,
        "\"##MaterialControlsBody\"",
        "donut::app::MaterialEditor(",
        "Materials controls inside light body plate");
    ExpectOrdered(
        materialEditor,
        "donut::app::MaterialEditor(",
        "ImGui::EndChild();",
        "Materials plate closes after editor controls");
    ExpectContains(
        materialEditor,
        "materialControlsDrawList =\n"
        "                    ImGui::GetWindowDrawList();",
        "Materials child appearance draw list capture");
    ExpectContains(
        materialEditor,
        "if (materialControlsDrawList &&\n"
        "            materialControlsDrawList != materialWindowDrawList)",
        "Materials child appearance transform guard");
    ExpectContains(
        materialEditor,
        "UiMaterialTitleBackdropIndex",
        "Material title blur mask");
    ExpectContains(
        materialEditor,
        "UiMaterialBodyBackdropIndex",
        "Material body blur mask");
    ExpectContains(
        materialEditor,
        "materialTitleBackdrop.rounding = style.FrameRounding;",
        "Material drawer-header blur radius");
    ExpectContains(
        materialEditor,
        "materialBodyBackdrop.rounding = style.WindowRounding;",
        "Material body blur radius");
    ExpectContains(
        materialEditor,
        "ApplyBackdropAppearance(",
        "Material backdrop zoom-and-fade transform");
    ExpectOrdered(
        materialEditor,
        "ImGui::End();",
        "ApplyWindowAppearance(",
        "post-layout Material window appearance transform");
    ExpectContains(
        materialEditor,
        "ImGui::PopStyleColor(4);",
        "balanced Materials title and body colors");
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
    ExpectContains(
        donutAppOverride,
        "-    const ImVec4 filenameColor = ImVec4(0.474f, 0.722f, 0.176f, 1.0f);\n"
        "+    const ImVec4 filenameColor = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);",
        "exact drawer-blue Material texture annotations");
    ExpectContains(
        source,
        "tokens.drawerHeader =\n"
        "                ImVec4(0.26f, 0.59f, 0.98f, 0.31f);",
        "authored drawer-blue RGB source");
    ExpectAbsent(
        donutAppOverride,
        "+    const ImVec4 filenameColor = ImVec4(0.474f, 0.722f, 0.176f, 1.0f);",
        "retired green Material texture annotation addition");
    ExpectAbsent(
        donutAppOverride,
        "+    const ImVec4 filenameColor = ImVec4(0.22f, 0.78f, 0.98f, 1.0f);",
        "retired cyan Material texture annotation addition");

    const std::string_view rendererPresentation = ExtractSection(
        source,
        "    virtual void Render(nvrhi::IFramebuffer* framebuffer) override",
        "    virtual void BackBufferResizing() override",
        "renderer zoom and crosshair presentation");
    ExpectContains(
        rendererPresentation,
        "ShouldDelayPixelZoomForMaterialInspector(",
        "Material Inspector-first zoom opening");
    ExpectContains(
        rendererPresentation,
        "AdvanceMaterialInspectorZoomPlacement(",
        "animated Material Inspector zoom placement");
    ExpectOrdered(
        rendererPresentation,
        "AdvanceMaterialInspectorAppearance(",
        "AdvanceMaterialInspectorZoomPlacement(",
        "Material appearance before zoom-relative placement");
    ExpectContains(
        rendererPresentation,
        "IsMaterialInspectorPresentationActive(",
        "retained Material presentation state");
    ExpectContains(
        rendererPresentation,
        "m_MaterialInspectorZoomPlacement",
        "zoom opening placement dependency");
    ExpectContains(
        rendererPresentation,
        "pixelZoomRequestedByUi &&\n"
        "            !pixelZoomOpeningDelayed",
        "delayed zoom opening request");
    ExpectContains(
        rendererPresentation,
        "const float crosshairOpacity = std::max(",
        "maximum zoom-or-inspector crosshair opacity");
    ExpectContains(
        rendererPresentation,
        "pixelZoomRequested ? pixelZoomOpacity : 0.f",
        "zoom crosshair opacity source");
    ExpectContains(
        rendererPresentation,
        "materialInspectorOpacity",
        "eased Material Inspector crosshair opacity source");
    ExpectAbsent(
        rendererPresentation,
        "m_ui.ShowMaterialEditor ? 1.f : 0.f",
        "removed snapping Material Inspector crosshair opacity");
    ExpectContains(
        rendererPresentation,
        "if (crosshairOpacity > 0.f)",
        "crosshair maximum visibility policy");
    ExpectContains(
        rendererPresentation,
        "m_app->IsSvsmMotionBenchmarkRunning()",
        "SVSM benchmark zoom suppression");
    ExpectContains(
        source,
        "const bool materialInspectorAppearanceIdle =\n"
        "                    IsMaterialInspectorAppearanceIdle(",
        "Material appearance deferred-commit barrier");
    ExpectContains(
        source,
        "materialInspectorPlacementIdle &&\n"
        "                    materialInspectorAppearanceIdle &&",
        "Material presentation idle composition gate");

    const std::string_view materialPickPurpose = ExtractSection(
        source,
        "    enum class MaterialPickPurpose",
        "    std::shared_ptr<RootFileSystem>",
        "typed material-pick purpose");
    ExpectContains(
        materialPickPurpose,
        "FocusCameraAtCursor",
        "focus-camera material-pick purpose");
    ExpectContains(
        materialPickPurpose,
        "OpenCenterMaterialInspector",
        "center Material Inspector pick purpose");
    const std::string_view mouseMaterialPickRouting = ExtractSection(
        source,
        "    virtual bool MousePosUpdate(",
        "    void AdvanceAntiAliasingTimer()",
        "cursor material-pick routing");
    ExpectContains(
        mouseMaterialPickRouting,
        "MaterialPickPurpose::FocusCameraAtCursor",
        "middle-click focus-camera pick purpose");
    const std::string_view centerInspectorToggle = ExtractSection(
        source,
        "    void ToggleCenterMaterialInspector()",
        "    const Material* GetOriginalMaterial(",
        "center Material Inspector toggle");
    ExpectContains(
        centerInspectorToggle,
        "MaterialPickPurpose::OpenCenterMaterialInspector",
        "center Material Inspector pick purpose");
    ExpectContains(
        centerInspectorToggle,
        "m_MaterialPickScene = m_Scene.get();",
        "center Material Inspector scene identity");
    const std::string_view sceneUnloadMaterialPick = ExtractSection(
        source,
        "    virtual void SceneUnloading() override",
        "    virtual bool LoadScene(",
        "scene-unload material-pick cancellation");
    ExpectContains(
        sceneUnloadMaterialPick,
        "m_MaterialPickPurpose = MaterialPickPurpose::None;",
        "scene-unload material-pick cancellation");
    ExpectContains(
        sceneUnloadMaterialPick,
        "m_MaterialPickScene = nullptr;",
        "scene-unload material-pick scene release");
    const std::string_view materialPickSubmission = ExtractSection(
        source,
        "        if (m_MaterialPickPurpose != MaterialPickPurpose::None &&",
        "        if (m_ui.ShowEnvironmentBackground &&",
        "material-pick submission");
    ExpectContains(
        materialPickSubmission,
        "m_MaterialPickScene != m_Scene.get()",
        "stale material-pick cancellation");
    const std::string_view centerPickSubmission = ExtractSection(
        materialPickSubmission,
        "        if (m_MaterialPickPurpose ==\n"
        "            MaterialPickPurpose::OpenCenterMaterialInspector)",
        "        if (m_MaterialPickPurpose != MaterialPickPurpose::None)",
        "center Material Inspector capture coordinate");
    ExpectContains(
        centerPickSubmission,
        "m_RenderTargets->MaterialIDs->getDesc()",
        "MaterialIDs capture dimensions");
    ExpectContains(
        centerPickSubmission,
        "ResolveCenterMaterialPick(",
        "exact MaterialIDs center coordinate");
    ExpectContains(
        centerPickSubmission,
        "m_PickPosition =\n"
        "                    uint2(centerPick.x, centerPick.y);",
        "exact center capture coordinate");
    ExpectAbsent(
        centerPickSubmission,
        "PointThirdPersonCameraAt(",
        "center capture camera isolation");
    ExpectOrdered(
        materialPickSubmission,
        "uint2(centerPick.x, centerPick.y);",
        "m_PixelReadbackPass->Capture(",
        "center coordinate before MaterialIDs capture");
    const std::string_view materialPickCompletion = ExtractSection(
        source,
        "        if (m_MaterialPickPurpose != MaterialPickPurpose::None)\n"
        "        {\n"
        "            const MaterialPickPurpose completedPurpose =",
        "    std::shared_ptr<ShaderFactory> GetShaderFactory()",
        "material-pick completion");
    ExpectContains(
        materialPickCompletion,
        "const bool completedForCurrentScene =",
        "stale material-pick completion cancellation");
    const std::string_view centerPickCompletion = ExtractSection(
        materialPickCompletion,
        "            if (completedPurpose ==\n"
        "                MaterialPickPurpose::OpenCenterMaterialInspector)",
        "            else if (completedForCurrentScene &&",
        "center Material Inspector completion");
    ExpectContains(
        centerPickCompletion,
        "m_ui.ShowMaterialEditor =",
        "center Material Inspector opening");
    ExpectAbsent(
        centerPickCompletion,
        "PointThirdPersonCameraAt(",
        "center Material Inspector camera isolation");
    ExpectContains(
        materialPickCompletion,
        "MaterialPickPurpose::FocusCameraAtCursor",
        "focus-camera completion routing");
    ExpectContains(
        materialPickCompletion,
        "PointThirdPersonCameraAt(",
        "focus-camera-only material-pick mutation");
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
        CountOccurrences(lights, "ImGui::Selectable(") != 0 ||
        CountOccurrences(shadows, "ImGui::Selectable(") != 0)
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
