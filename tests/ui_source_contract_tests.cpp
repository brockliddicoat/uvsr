#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    int g_FailureCount = 0;

    void Fail(const std::string& message)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++g_FailureCount;
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        if (!stream)
            return {};
        return std::string(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }

    size_t CountOccurrences(
        std::string_view source,
        std::string_view value)
    {
        if (value.empty())
            return 0u;
        size_t count = 0u;
        size_t position = 0u;
        while ((position = source.find(value, position)) !=
            std::string_view::npos)
        {
            ++count;
            position += value.size();
        }
        return count;
    }

    std::string Compact(std::string_view source)
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

    std::string AddedPatchLines(std::string_view patch)
    {
        std::string additions;
        size_t cursor = 0u;
        while (cursor < patch.size())
        {
            const size_t lineEnd = patch.find('\n', cursor);
            const size_t length =
                lineEnd == std::string_view::npos
                    ? patch.size() - cursor
                    : lineEnd - cursor;
            const std::string_view line = patch.substr(cursor, length);
            if (!line.empty() && line.front() == '+' &&
                !(line.size() >= 3u && line.substr(0u, 3u) == "+++"))
            {
                additions.append(line.substr(1));
                additions.push_back('\n');
            }
            if (lineEnd == std::string_view::npos)
                break;
            cursor = lineEnd + 1u;
        }
        return additions;
    }

    std::string_view ExtractSection(
        std::string_view source,
        std::string_view begin,
        std::string_view end,
        std::string_view label)
    {
        const size_t beginPosition = source.find(begin);
        if (beginPosition == std::string_view::npos)
        {
            Fail(std::string(label) + " is missing its begin anchor.");
            return {};
        }
        const size_t endPosition = source.find(
            end,
            beginPosition + begin.size());
        if (endPosition == std::string_view::npos)
        {
            Fail(std::string(label) + " is missing its end anchor.");
            return {};
        }
        return source.substr(beginPosition, endPosition - beginPosition);
    }

    void RequireContains(
        std::string_view source,
        std::string_view value,
        std::string_view label)
    {
        Require(
            source.find(value) != std::string_view::npos,
            std::string(label) + " must contain '" +
                std::string(value) + "'.");
    }

    void RequireAbsent(
        std::string_view source,
        std::string_view value,
        std::string_view label)
    {
        Require(
            source.find(value) == std::string_view::npos,
            std::string(label) + " must not contain '" +
                std::string(value) + "'.");
    }

    void RequireOrdered(
        std::string_view source,
        const std::vector<std::string_view>& values,
        std::string_view label)
    {
        size_t cursor = 0u;
        for (const std::string_view value : values)
        {
            const size_t position = source.find(value, cursor);
            if (position == std::string_view::npos)
            {
                Fail(std::string(label) + " is missing ordered value '" +
                    std::string(value) + "'.");
                return;
            }
            cursor = position + value.size();
        }
    }

    std::vector<std::string> ParseQuotedStrings(std::string_view source)
    {
        std::vector<std::string> values;
        size_t cursor = 0u;
        while (true)
        {
            const size_t begin = source.find('"', cursor);
            if (begin == std::string_view::npos)
                break;
            const size_t end = source.find('"', begin + 1u);
            if (end == std::string_view::npos)
                break;
            values.emplace_back(source.substr(begin + 1u, end - begin - 1u));
            cursor = end + 1u;
        }
        return values;
    }

    void RequireExactStrings(
        std::string_view source,
        const std::vector<std::string>& expected,
        std::string_view label)
    {
        const std::vector<std::string> actual = ParseQuotedStrings(source);
        if (actual != expected)
        {
            Fail(std::string(label) + " has an unexpected label set or order.");
        }
    }

    struct CatalogEntry
    {
        std::string name;
        std::string section;
        bool action = false;
    };

    std::vector<CatalogEntry> ParseCatalog(std::string_view catalog)
    {
        constexpr std::string_view ValueAnchor = "Value(\"";
        constexpr std::string_view ActionAnchor = "Action(\"";
        constexpr std::string_view SectionAnchor = "Section::";
        std::vector<CatalogEntry> entries;
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
            const bool action = actionPosition != std::string_view::npos &&
                (valuePosition == std::string_view::npos ||
                    actionPosition < valuePosition);
            const size_t entryPosition = action
                ? actionPosition
                : valuePosition;
            const size_t nameBegin = entryPosition +
                (action ? ActionAnchor.size() : ValueAnchor.size());
            const size_t nameEnd = catalog.find('"', nameBegin);
            const size_t sectionBegin = catalog.find(
                SectionAnchor,
                nameEnd == std::string_view::npos
                    ? entryPosition
                    : nameEnd);
            if (nameEnd == std::string_view::npos ||
                sectionBegin == std::string_view::npos)
            {
                Fail("The Settings command catalog contains a malformed entry.");
                break;
            }
            const size_t sectionNameBegin =
                sectionBegin + SectionAnchor.size();
            size_t sectionNameEnd = sectionNameBegin;
            while (sectionNameEnd < catalog.size())
            {
                const unsigned char character = static_cast<unsigned char>(
                    catalog[sectionNameEnd]);
                if (!std::isalnum(character) && character != '_')
                    break;
                ++sectionNameEnd;
            }
            entries.push_back({
                std::string(catalog.substr(nameBegin, nameEnd - nameBegin)),
                std::string(catalog.substr(
                    sectionNameBegin,
                    sectionNameEnd - sectionNameBegin)),
                action
            });
            cursor = sectionNameEnd;
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

    void ValidateUiSpacing(
        std::string_view viewer,
        std::string_view uiCommandLayout)
    {
        const std::string compactLayout = Compact(uiCommandLayout);
        RequireContains(
            compactLayout,
            "inlineconstexprfloatUiSpacingBasePixels=4.f;",
            "four-pixel UI spacing base token");
        RequireContains(
            compactLayout,
            "floattight=UiSpacingBasePixels;"
            "floatregular=UiSpacingBasePixels*2.f;"
            "floatsection=UiSpacingBasePixels*4.f;",
            "4/8/16 unscaled UI spacing defaults");
        RequireContains(
            compactLayout,
            "constfloattight=UiSpacingBasePixels*safeScale;"
            "return{tight,tight*2.f,tight*4.f};",
            "scale-preserving Tight, Regular, and Section spacing ratios");
        RequireContains(
            compactLayout,
            "ResolveCommandInterfaceLayout("
            "constUiCommandLayoutRect&workRectangle,floatmargin,"
            "floatpanelToCommandGap,floatcompactWidth",
            "command layout accepts a distinct panel-to-command gap directly "
            "after the outer margin");
        RequireContains(
            compactLayout,
            "constfloatsafePanelToCommandGap="
            "std::max(0.f,panelToCommandGap);",
            "negative panel-to-command gap clamp");
        RequireContains(
            compactLayout,
            "result.settingsMaximumBottom=std::max("
            "innerTop,result.top-safePanelToCommandGap);",
            "Settings cap uses the independent clamped Tight gap");

        const std::string_view skinSpacing = ExtractSection(
            viewer,
            "const float safeDisplayScale =",
            "static void PushPanelBodySurface()",
            "active-skin spacing application");
        RequireContains(
            Compact(skinSpacing),
            "constUiSpacingTokensspacing="
            "ResolveUiSpacingTokens(safeDisplayScale);"
            "style.WindowPadding=ImVec2(spacing.regular,spacing.regular);"
            "ImGui::SetUvsrAuthoredWindowPadding(style.WindowPadding);"
            "style.ItemSpacing=ImVec2(spacing.regular,spacing.tight);"
            "style.ItemInnerSpacing=ImVec2(spacing.tight,spacing.tight);",
            "Regular window/item X spacing and Tight item Y/inner spacing");

        const std::string compactViewer = Compact(viewer);
        RequireContains(
            compactViewer,
            "std::round(g_UiSpacingTokens.section)",
            "Section-sized outer panel margin");
        RequireContains(
            compactViewer,
            "constfloatpanelSeparation=g_UiSpacingTokens.tight;",
            "Tight Performance-to-Settings gap");
        RequireContains(
            compactViewer,
            "ResolveCommandInterfaceLayout("
            "workRectangle,float(m_SettingsPanelMarginPixels),"
            "g_UiSpacingTokens.tight,260.f*m_UiDisplayScale",
            "Section-derived outer margin and Tight stack-to-command gap stay "
            "distinct at the production call site");
        RequireAbsent(
            viewer,
            "AddExactVerticalUiGap(",
            "retired child-owned Settings title-to-General gap");
        RequireContains(
            compactViewer,
            "constfloatactionButtonGap=g_UiSpacingTokens.tight;",
            "Tight footer action-button gap");
        const std::string_view interfaceDrawer = ExtractSection(
            viewer,
            "void DrawInterfaceDrawer(float settingsControlWidth)",
            "static std::string BuildPerformanceLine(",
            "final Interface drawer spacing");
        Require(
            CountOccurrences(interfaceDrawer, "ImGui::Spacing();") == 2u,
            "collapsed and expanded Interface drawers must each emit exactly "
            "one Tight ItemSpacing gap before the footer.");
    }

    void ValidateDrawers(std::string_view viewer)
    {
        const std::string_view generalDrawer = ExtractSection(
            viewer,
            "void DrawGeneralDrawer(float settingsControlWidth)",
            "void DrawMaterialDrawer(float settingsControlWidth)",
            "ordinary General drawer");
        const std::string compactGeneralDrawer = Compact(generalDrawer);
        RequireOrdered(
            generalDrawer,
            {
                "const bool generalOpen = DrawCollapsingHeader(",
                "\"General\"",
                "ImGuiTreeNodeFlags_DefaultOpen",
                "if (!generalOpen)",
                "ImGui::Spacing();",
                "return;",
                "BeginDrawerBody(\"##GeneralBody\", settingsControlWidth);",
                "EndDrawerBody();",
                "ImGui::Spacing();"
            },
            "default-open General ordinary drawer skeleton");
        Require(
            CountOccurrences(
                compactGeneralDrawer,
                "DrawCollapsingHeader(") == 1u &&
                CountOccurrences(
                    compactGeneralDrawer,
                    "BeginDrawerBody(") == 1u &&
                CountOccurrences(
                    compactGeneralDrawer,
                    "EndDrawerBody();") == 1u &&
                CountOccurrences(
                    compactGeneralDrawer,
                    "ImGui::Spacing();") == 2u,
            "General must use exactly one ordinary header/body lifetime and "
            "one runtime trailing gap on either branch.");
        for (const std::string_view generalGeometryHack : {
                std::string_view("ImGui::SetCursorPos"),
                std::string_view("PushClipRect"),
                std::string_view("ImGui::SetNextWindow"),
                std::string_view("ImGui::BeginChild") })
        {
            RequireAbsent(
                generalDrawer,
                generalGeometryHack,
                "General-specific root geometry and clipping");
        }

        struct Drawer
        {
            std::string_view begin;
            std::string_view end;
            std::string_view body;
            std::string_view label;
        };
        const std::vector<Drawer> drawers = {
            {
                "const bool representationOpen = DrawCollapsingHeader(",
                "const auto drawNoiseSettingsControls = [&] (",
                "##RepresentationBody",
                "Representation"
            },
            {
                "const bool noiseOpen = DrawCollapsingHeader(",
                "const auto drawRatioEstimatorShadowControls = [&]()",
                "##NoiseBody",
                "Noise"
            },
            {
                "const bool diffuseOpen = DrawCollapsingHeader(",
                "const bool denoisingOpen = DrawCollapsingHeader(",
                "##DiffuseBody",
                "Diffuse"
            },
            {
                "const bool denoisingOpen = DrawCollapsingHeader(",
                "const bool buffersOpen = DrawCollapsingHeader(",
                "##DenoisingBody",
                "Denoising"
            },
            {
                "const bool buffersOpen = DrawCollapsingHeader(",
                "const bool antiAliasingOpen = DrawCollapsingHeader(",
                "##BuffersBody",
                "Buffers"
            },
            {
                "const bool antiAliasingOpen = DrawCollapsingHeader(",
                "const bool debugOpen = DrawCollapsingHeader(",
                "##AliasingBody",
                "Aliasing"
            },
            {
                "const bool debugOpen = DrawCollapsingHeader(",
                "const bool skyOpen = DrawCollapsingHeader(",
                "##DebugBody",
                "Debug"
            },
            {
                "const bool skyOpen = DrawCollapsingHeader(",
                "const auto& lights = m_app->GetEditableLights();",
                "##SkyBody",
                "Sky"
            },
            {
                "const bool lightsOpen = DrawCollapsingHeader(",
                "const bool shadowsOpen = DrawCollapsingHeader(",
                "##LightsBody",
                "Lights"
            },
            {
                "const bool shadowsOpen = DrawCollapsingHeader(",
                "DrawMaterialDrawer(settingsControlWidth);",
                "##ShadowsBody",
                "Shadows"
            }
        };

        for (const Drawer& drawer : drawers)
        {
            const std::string_view section = ExtractSection(
                viewer,
                drawer.begin,
                drawer.end,
                drawer.label);
            const std::string compact = Compact(section);
            Require(
                ContainsQuotedLiteral(section, drawer.label),
                std::string(drawer.label) +
                    " drawer must retain its exact visible header label.");
            RequireContains(
                compact,
                "BeginDrawerBody(\"" + std::string(drawer.body) +
                    "\",settingsControlWidth);",
                std::string(drawer.label) + " drawer body");
            Require(
                CountOccurrences(compact, "BeginDrawerBody(") == 1u &&
                    CountOccurrences(compact, "EndDrawerBody();") == 1u,
                std::string(drawer.label) +
                    " drawer must balance one body begin and end.");
        }

        for (const std::string_view retiredDrawer : {
                std::string_view("Sparse Virtual Shadow Maps##Shadows"),
                std::string_view(
                    "Diagnostic Cascaded Shadow Maps##Shadows") })
        {
            RequireAbsent(viewer, retiredDrawer, "cleaned Settings drawers");
        }

        const std::string_view settingsDrawers = ExtractSection(
            viewer,
            "DrawGeneralDrawer(settingsControlWidth);",
            "constexpr float ActionButtonCount = 4.f;",
            "Settings drawer order");
        RequireOrdered(
            settingsDrawers,
            {
                "DrawGeneralDrawer(settingsControlWidth);",
                "const bool representationOpen = DrawCollapsingHeader(",
                "const bool noiseOpen = DrawCollapsingHeader(",
                "const bool diffuseOpen = DrawCollapsingHeader(",
                "const bool denoisingOpen = DrawCollapsingHeader(",
                "const bool buffersOpen = DrawCollapsingHeader(",
                "const bool antiAliasingOpen = DrawCollapsingHeader(",
                "const bool debugOpen = DrawCollapsingHeader(",
                "const bool skyOpen = DrawCollapsingHeader(",
                "const bool lightsOpen = DrawCollapsingHeader(",
                "const bool shadowsOpen = DrawCollapsingHeader(",
                "DrawMaterialDrawer(settingsControlWidth);",
                "DrawInterfaceDrawer(settingsControlWidth);",
                "TrackSettingsScrollAnchor("
            },
            "General-first Settings order with Material immediately before "
            "the final Interface drawer");
        Require(
            CountOccurrences(
                viewer,
                "DrawGeneralDrawer(settingsControlWidth);") == 1u,
            "Settings must submit the extracted General drawer exactly once.");
        const std::string_view generalSubmissionScope = ExtractSection(
            viewer,
            "BeginSettingsScrollStability();",
            "const bool representationOpen = DrawCollapsingHeader(",
            "General Settings submission scope");
        RequireOrdered(
            generalSubmissionScope,
            {
                "BeginSettingsScrollStability();",
                "const bool deferredDropdownInputBlocked =",
                "ImGui::BeginDisabled();",
                "const bool settingsScrollInputBlocked =",
                "ImGui::BeginDisabled();",
                "DrawGeneralDrawer(settingsControlWidth);"
            },
            "General remains first inside the shared deferred-input and scroll "
            "stability scopes");
        const std::string_view interfaceDrawer = ExtractSection(
            viewer,
            "void DrawInterfaceDrawer(float settingsControlWidth)",
            "static std::string BuildPerformanceLine(",
            "Interface drawer controls");
        RequireOrdered(
            interfaceDrawer,
            {
                "\"Interface\"",
                "bool disableAnimations = !m_ui.AnimationsEnabled;",
                "ImGui::Checkbox(",
                "\"Disable Animations\"",
                "&disableAnimations",
                "m_ui.AnimationsEnabled = !disableAnimations;",
                "\"Override Visual Maxes\"",
                "&m_ui.OverrideVisualMaxes",
                "\"Interface Skin\"",
                "\"##UiSkin\"",
                "for (const UiSkin candidate : UiSkinValues)",
                "DrawDeferredDropdownOption(",
                "UiSkinLabel(candidate).data(),",
                "FindUiSkinPalette(m_ui.Accents, m_ui.Skin)",
                "\"Primary Accent\"",
                "editablePalette.primaryAccent",
                "\"Secondary Accent\"",
                "m_ui.Accents.secondaryAccent",
                "\"Tertiary Accent\"",
                "m_ui.Accents.tertiaryAccent",
                "\"Font Color\"",
                "editablePalette.fontColor",
                "\"Primary Background Color\"",
                "editablePalette.primaryBackground"
            },
            "Interface starts with the inverse-bound animation checkbox, then "
            "owns the skin and all five directly visible color roles");
        RequireAbsent(
            interfaceDrawer,
            "Advanced Accents",
            "retired Advanced Accents submenu");
        RequireContains(
            interfaceDrawer,
            "\"Set menu body, resting controls, and picker background; \"\n"
            "                    \"hover, active, body, and picker opacity derive from it.\"",
            "Primary Background tooltip ownership of the derived picker surface");
        RequireAbsent(
            interfaceDrawer,
            "\"Enable Animations\"",
            "Interface animation control has one stable inverse-bound label");
        RequireContains(
            viewer,
            "bool                                AnimationsEnabled = true;",
            "Interface animations default enabled on each launch");
        RequireContains(
            viewer,
            "bool                                OverrideVisualMaxes = false;",
            "slider visual-maximum override default disabled on each launch");
        RequireOrdered(
            interfaceDrawer,
            {
                "float values[4] =",
                "DrawUvsrColorEdit(",
                "UvsrColorEditChannels::Rgba",
                "values[3]"
            },
            "all Interface roles route through the shared RGBA color editor");
        RequireContains(
            interfaceDrawer,
            "const bool authoredPaletteAvailable =\n"
            "            palette != nullptr && defaultPalette != nullptr;",
            "Ogg disables authored palette roles");
        Require(
            CountOccurrences(
                interfaceDrawer,
                "authoredPaletteAvailable);") == 3u,
            "Ogg must disable Primary Accent, Font Color, and Primary Background "
            "while leaving shared Secondary and Tertiary semantic colors live.");
        for (const std::string_view retiredRole : {
                std::string_view("primaryFont"),
                std::string_view("secondaryFont"),
                std::string_view("secondaryBackground"),
                std::string_view("Primary Font Color"),
                std::string_view("Secondary Font Color"),
                std::string_view("Secondary Background Color") })
        {
            RequireAbsent(
                interfaceDrawer,
                retiredRole,
                "retired split Interface palette role");
        }
    }

    void ValidateRepresentation(
        std::string_view viewer,
        std::string_view catalog)
    {
        const std::string_view representation = ExtractSection(
            viewer,
            "const bool representationOpen = DrawCollapsingHeader(",
            "const auto drawNoiseSettingsControls = [&] (",
            "Representation drawer");
        RequireContains(
            representation,
            "\"Allow Ray Traversal\"",
            "ray traversal master toggle label");
        RequireContains(
            representation,
            "&representation.allowRayTraversal",
            "ray traversal master toggle state");
        RequireContains(
            catalog,
            "representation.allow-ray-traversal",
            "ray traversal master command");

        const std::string_view representationStatus = ExtractSection(
            representation,
            "const char* representationState = \"Inactive\";",
            "if (representationStatus.totalBlasCount > 0u)",
            "Representation status presentation");
        RequireOrdered(
            representationStatus,
            {
                "case WorldSpaceRepresentationState::Idle:",
                "default:",
                "if (representationStatus.state ==",
                "WorldSpaceRepresentationState::Idle)",
                "ImGui::TextDisabled(\"Status: Inactive\");",
                "else",
                "ImGui::Text(\"Status: %s\", representationState);"
            },
            "only the idle Representation state uses disabled status text");
        Require(
            CountOccurrences(
                representationStatus,
                "ImGui::TextDisabled(\"Status: Inactive\");") == 1u &&
                CountOccurrences(
                    representationStatus,
                    "ImGui::Text(\"Status: %s\", representationState);") ==
                    1u,
            "Representation must keep one idle-only disabled status and one "
            "ordinary presentation for every non-idle state.");
        RequireAbsent(
            representation,
            "Builds lazily when a ray traced technique needs it.",
            "retired Representation lazy-creation status text");
    }

    void ValidateNoise(
        std::string_view viewer,
        std::string_view catalog)
    {
        const std::string_view controls = ExtractSection(
            viewer,
            "const auto drawNoiseSettingsControls = [&] (",
            "const bool noiseOpen = DrawCollapsingHeader(",
            "shared Noise controls");
        RequireExactStrings(
            ExtractSection(
                controls,
                "static constexpr const char* PatternLabels[] = {",
                "};",
                "Noise pattern labels"),
            {
                "Spatial White",
                "Spatial Blue",
                "Spatiotemporal Blue"
            },
            "Noise pattern labels");
        RequireExactStrings(
            ExtractSection(
                controls,
                "static constexpr const char* ResolutionLabels[] = {",
                "};",
                "Noise resolution labels"),
            { "64x64", "128x128", "256x256", "512x512" },
            "Noise resolution labels");
        RequireOrdered(
            controls,
            {
                "Noise Pattern##",
                "Noise Resolution##",
                "Animate Samples##"
            },
            "shared Noise control order");
        RequireContains(
            controls,
            "Choose the centered tile resolution used by this noise",
            "centered Noise tiling tooltip");

        const std::string_view drawer = ExtractSection(
            viewer,
            "const bool noiseOpen = DrawCollapsingHeader(",
            "const auto drawRatioEstimatorShadowControls = [&]()",
            "Noise drawer");
        RequireContains(
            drawer,
            "Configure the shared precomputed noise used by rendering effects.",
            "shared Noise drawer description");
        RequireOrdered(
            drawer,
            {
                "drawNoiseSettingsControls(",
                "m_ui.Noise,",
                "\"GlobalNoise\"",
                "m_app->ResetNoiseSamplingHistory("
            },
            "global Noise configuration and history reset");

        for (const std::string_view label : {
                std::string_view("Specify Noise##Visibility"),
                std::string_view("Specify Noise##RatioEstimatorShadows"),
                std::string_view(
                    "Specify Noise##RayTracedSkyVisibility") })
        {
            RequireContains(viewer, label, "per-effect Specify Noise control");
            const std::string quotedLabel =
                "\"" + std::string(label) + "\"";
            const std::string_view tooltipSection = ExtractSection(
                viewer,
                quotedLabel,
                "if (DrawPresetResetIcon(",
                "Specify Noise tooltip");
            const std::vector<std::string> literals =
                ParseQuotedStrings(tooltipSection);
            std::string tooltip;
            for (size_t index = 1u; index < literals.size(); ++index)
                tooltip += literals[index];
            constexpr std::string_view IsolationTooltip =
                "Use custom noise sampling for this effect only. This does "
                "not change the noise sampling used by any other effect.";
            Require(
                tooltip.rfind(IsolationTooltip, 0u) == 0u,
                std::string(label) +
                    " must use the exact cross-effect isolation tooltip.");
        }
        Require(
            CountOccurrences(viewer, "drawNoiseSettingsControls(") == 4u,
            "one global and three custom noise control groups must remain visible.");

        const std::string compactCatalog = Compact(catalog);
        for (const std::string_view command : {
                std::string_view(
                    "Value(\"noise.pattern\",Kind::Enum,Section::Noise,"
                    "\"spatial-white|spatial-blue|spatiotemporal-blue\")"),
                std::string_view(
                    "Value(\"noise.resolution\",Kind::Enum,Section::Noise,"
                    "\"64x64|128x128|256x256|512x512\")"),
                std::string_view(
                    "Value(\"noise.animate-samples\",Kind::Boolean,"
                    "Section::Noise,\"on|off\")") })
        {
            RequireContains(
                compactCatalog,
                command,
                "global Noise command domain");
        }
        for (const std::string_view legacyLabel : {
                std::string_view("Permutated White Noise"),
                std::string_view("Void Cluster Blue Noise") })
        {
            RequireAbsent(viewer, legacyLabel, "renamed Noise labels");
        }
    }

    void ValidateVisibility(
        std::string_view viewer,
        std::string_view catalog)
    {
        const std::string_view visibility = ExtractSection(
            viewer,
            "const bool diffuseOpen = DrawCollapsingHeader(",
            "const bool denoisingOpen = DrawCollapsingHeader(",
            "Diffuse drawer");
        RequireContains(
            visibility,
            "MarkScreenSpaceVisibilityQualityCustom(settings)",
            "Visibility custom-preset tracking");
        RequireContains(
            visibility,
            "std::string profilePreview = QualityLabels[profileIndex];",
            "Visibility originating-profile preview");
        RequireContains(
            visibility,
            "visibility.quality ==\n"
            "                ScreenSpaceVisibilityQuality::Custom",
            "Visibility custom-profile preview condition");
        RequireContains(
            visibility,
            "profilePreview += \" (Custom)\";",
            "Visibility custom-profile preview suffix");
        RequireContains(
            visibility,
            "the shared Noise configuration",
            "Visibility profiles inherit the shared Noise configuration");
        RequireAbsent(
            visibility,
            "Spatial Blue noise",
            "Visibility profiles must not promise a fixed Noise pattern");
        RequireContains(
            visibility,
            "profilePreview.c_str()",
            "Visibility profile preview string lifetime");
        RequireContains(
            visibility,
            "DrawPresetResetIcon(\n                    \"VisibilityProfile\"",
            "Visibility profile reset");
        RequireExactStrings(
            ExtractSection(
                visibility,
                "static constexpr const char* QualityLabels[] = {",
                "};",
                "Visibility profile labels"),
            { "Low", "Medium", "High", "Ultra" },
            "Visibility profile labels");
        RequireOrdered(
            visibility,
            {
                "\"Occlusion###Ambient Occlusion##Visibility\"",
                "\"Illumination###Indirect Diffuse##Visibility\"",
                "\"Sampling##Visibility\"",
                "\"Reconstruction##Visibility\""
            },
            "Diffuse effect grouping");
        RequireExactStrings(
            ExtractSection(
                visibility,
                "static constexpr const char* EstimatorLabels[] = {",
                "};",
                "Diffuse estimator labels"),
            {
                "Bitmask Approximation",
                "Bitmask Directional Visibility",
                "Bitmask Cosine Visibility"
            },
            "Diffuse estimator labels");
        Require(
            CountOccurrences(visibility, "BeginAnimatedTreeNode(") == 4u,
            "Visibility must retain four animated effect groups.");
        RequireContains(
            Compact(visibility),
            "constImGuiTreeNodeFlagsreconstructionDisclosureFlags="
            "visibility.resolution==VisibilityResolution::Full?"
            "ImGuiTreeNodeFlags_None:ImGuiTreeNodeFlags_DefaultOpen;",
            "resolution-aware Reconstruction disclosure default");
        RequireOrdered(
            visibility,
            {
                "const ImGuiTreeNodeFlags reconstructionDisclosureFlags =",
                "\"Reconstruction##Visibility\"",
                "reconstructionDisclosureFlags"
            },
            "Reconstruction disclosure flag use");
        Require(
            CountOccurrences(visibility, "ImGui::SetItemTooltip(") >= 12u,
            "Visibility controls must retain their hover guidance.");
        for (const std::string_view control : {
                std::string_view("&visibility.ambientOcclusion.enabled"),
                std::string_view(
                    "&visibility.ambientOcclusion.outputHitDistance"),
                std::string_view("&visibility.indirectDiffuse.enabled"),
                std::string_view(
                    "&visibility.indirectDiffuse.outputHitDistance"),
                std::string_view(
                    "visibility.sampling.maximumSampleCount"),
                std::string_view("&visibility.noise.specifyNoise"),
                std::string_view("visibility.reconstruction.mode"),
                std::string_view(
                    "&visibility.reconstruction.spatialEnabled") })
        {
            RequireContains(visibility, control, "Visibility core control");
        }
        RequireContains(
            Compact(catalog),
            "Value(\"visibility.specify-noise\",Kind::Boolean,"
            "Section::Visibility,\"on|off\")",
            "Visibility custom noise command");
        RequireContains(
            Compact(catalog),
            "Value(\"visibility.noise-resolution\",Kind::Enum,"
            "Section::Visibility,\"64x64|128x128|256x256|512x512\")",
            "Visibility custom noise resolution domain");
        RequireContains(
            Compact(catalog),
            "Value(\"visibility.distribution\",Kind::Float,"
            "Section::Visibility,\"float0.25..8\")",
            "Visibility distribution command range");
        RequireContains(
            Compact(catalog),
            "Value(\"visibility.ao.strength\",Kind::Float,"
            "Section::Visibility,\"float0..8\")",
            "ambient occlusion command range");
        for (const std::string_view reconstructionLabel : {
                std::string_view("Full Resolution"),
                std::string_view("Guide-Aware Upsampling"),
                std::string_view("Packed Depth-Normal"),
                std::string_view("Packed Slope-Aware"),
                std::string_view("Packed Leak-Controlled") })
        {
            RequireContains(
                visibility,
                reconstructionLabel,
                "Visibility reconstruction label");
        }
        RequireAbsent(
            visibility,
            "\"Packed Depth\"",
            "Visibility reconstruction selector");
        RequireAbsent(
            visibility,
            "ambientOcclusion.power",
            "single ambient-occlusion strength control");
        for (const std::string_view retired : {
                std::string_view("Toroidal Blue"),
                std::string_view("Independent Hash"),
                std::string_view("Depth Hierarchy"),
                std::string_view("Temporal Accumulation"),
                std::string_view("Sample Resurrection"),
                std::string_view("Later Bounce"),
                std::string_view("Fused Resolve"),
                std::string_view("VisibilityBenchmark") })
        {
            RequireAbsent(visibility, retired, "cleaned Visibility drawer");
        }
    }

    void ValidateDenoising(
        std::string_view viewer,
        std::string_view catalog)
    {
        const std::string_view denoising = ExtractSection(
            viewer,
            "const bool denoisingOpen = DrawCollapsingHeader(",
            "const bool buffersOpen = DrawCollapsingHeader(",
            "Denoising drawer");
        RequireOrdered(
            denoising,
            {
                "Occlusion###Ambient Occlusion##Denoising",
                "Illumination###Diffuse GI##Denoising",
                "Shadows##Denoising",
                "Sky Visibility##Denoising"
            },
            "Denoising signal order");
        Require(
            CountOccurrences(denoising, "drawDenoisingSignal(") == 4u,
            "Denoising must expose exactly four independent signal groups.");
        for (const std::string_view contract : {
                std::string_view("SupportsDenoisingMethod(effect, method)"),
                std::string_view("GetDenoisingQualityLabel(signal.quality)"),
                std::string_view(
                    "GetDenoisingResolutionLabel(signal.resolution)"),
                std::string_view("signal.historyLength"),
                std::string_view("signal.disocclusionThreshold"),
                std::string_view("signal.antiLagStrength") })
        {
            RequireContains(denoising, contract, "Denoising signal controls");
        }
        RequireContains(
            denoising,
            "m_ui.ScreenSpaceVisibility.ambientOcclusion.outputHitDistance",
            "ambient occlusion hit distance readiness");
        RequireContains(
            denoising,
            "m_ui.ScreenSpaceVisibility.indirectDiffuse.outputHitDistance",
            "diffuse GI hit distance readiness");
        RequireContains(
            denoising,
            "!m_ui.DirectionalShadows.ratioEstimator.useRatioEstimator",
            "sun raw sample readiness");
        RequireContains(
            denoising,
            "m_ui.Flashlight.outputHitDistance",
            "flashlight hit distance readiness");
        RequireContains(
            denoising,
            "!m_ui.RayTracedSkyVisibility.useRatioEstimator",
            "sky raw sample readiness");
        RequireContains(
            denoising,
            "#if UVSR_WITH_NRD",
            "build-time NRD availability message");
        RequireContains(
            denoising,
            "effect != DenoisingEffect::Shadows",
            "unsupported SIGMA history and response control gate");
        RequireContains(
            denoising,
            "Flashlight SIGMA is spatial only",
            "flashlight SIGMA history disclosure");
        const std::string_view nrdDisabledFooter = ExtractSection(
            denoising,
            "#if !UVSR_WITH_NRD",
            "EndDrawerBody();",
            "NRD-disabled Denoising footer");
        RequireExactStrings(
            nrdDisabledFooter,
            { "NRD is not actually included in this build" },
            "NRD-disabled Denoising footer");
        RequireOrdered(
            denoising,
            {
                "\"Sky Visibility##Denoising\"",
                "#if !UVSR_WITH_NRD",
                "ImGui::TextDisabled(",
                "\"NRD is not actually included in this build\");",
                "#endif",
                "EndDrawerBody();"
            },
            "the exact NRD-disabled message remains the final Denoising "
            "drawer content");
        for (const std::string_view path : {
                std::string_view("denoising.ao.method"),
                std::string_view("denoising.gi.method"),
                std::string_view("denoising.shadows.method"),
                std::string_view("denoising.sky.method") })
        {
            RequireContains(catalog, path, "Denoising command catalog");
        }
    }

    void ValidateBuffers(std::string_view viewer)
    {
        const std::string_view buffers = ExtractSection(
            viewer,
            "const bool buffersOpen = DrawCollapsingHeader(",
            "const bool antiAliasingOpen = DrawCollapsingHeader(",
            "Buffers drawer");
        RequireExactStrings(
            ExtractSection(
                buffers,
                "static constexpr const char* BufferProfileLabels[] = {",
                "};",
                "buffer profile labels"),
            {
                "Performance",
                "Maximum Precision",
                "Compact Occlusion",
                "Compact Indirect"
            },
            "buffer profile labels");
        RequireExactStrings(
            ExtractSection(
                buffers,
                "static constexpr const char* PrecisionLabels[] = {",
                "};",
                "buffer precision labels"),
            { "16-Bit Floating Point", "32-Bit Floating Point" },
            "buffer precision labels");
        for (const std::string_view control : {
                std::string_view("Profile##Buffers"),
                std::string_view("\"Occlusion###Ambient Occlusion\""),
                std::string_view("\"Illumination###Indirect Diffuse\""),
                std::string_view("visibility.bufferPrecision.ambient"),
                std::string_view("visibility.bufferPrecision.indirect"),
                std::string_view("VisibilityBufferProfile") })
        {
            RequireContains(buffers, control, "compact buffer control");
        }
        for (const std::string_view retired : {
                std::string_view("Temporal History"),
                std::string_view("Bounce"),
                std::string_view("Depth Hierarchy"),
                std::string_view("Benchmark") })
        {
            RequireAbsent(buffers, retired, "compact Buffers drawer");
        }
        for (const std::string_view visibleRetiredTerm : {
                std::string_view("\"Ambient Occlusion\""),
                std::string_view("\"Indirect Diffuse\""),
                std::string_view("ambient occlusion"),
                std::string_view("indirect diffuse") })
        {
            RequireAbsent(
                buffers,
                visibleRetiredTerm,
                "renamed Buffers UI");
        }
    }

    void ValidatePerformance(std::string_view viewer)
    {
        const std::string_view performanceBuilders = ExtractSection(
            viewer,
            "static std::string BuildPerformanceLine(",
            "template <typename... Arguments>",
            "performance summary builders");
        Require(
            CountOccurrences(performanceBuilders, "\" / \"") == 5u,
            "the one-line authored and two-line Ogg panel summaries must "
            "retain all five slash field separators.");
        RequireAbsent(
            performanceBuilders,
            "\" - \"",
            "slash-separated performance summaries");
        const std::string_view statistics = ExtractSection(
            viewer,
            "void DrawPerformancePanelContents(",
            "void DrawGeneralDrawer(",
            "Performance panel contents");
        RequireAbsent(
            statistics,
            "DrawCollapsingHeader(",
            "nested Performance drawer header");
        RequireContains(
            statistics,
            "ImGui::TextUnformatted(performanceLine.c_str())",
            "authored-skin Performance panel summary");
        for (const std::string_view ogPanelContract : {
                std::string_view("m_ComposedUiSkin == UiSkin::Og"),
                std::string_view("ogPerformanceLines[0].c_str()"),
                std::string_view("ogPerformanceLines[1].c_str()") })
        {
            RequireContains(
                statistics,
                ogPanelContract,
                "Ogg two-line Performance panel summary");
        }
        RequireContains(
            statistics,
            "ImGui::SetItemTooltip(\"%s\", performanceTooltip)",
            "Performance panel triangle-count explanation");
        RequireOrdered(
            statistics,
            {
                "const StatisticsEffect selectedEffect =",
                "const auto drawStatisticsTable =",
                "[&](StatisticsEffect effect)",
                "switch (effect)",
                "ImGui::PushStyleVar(",
                "ImGuiStyleVar_ItemSpacing,",
                "ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.f));",
                "for (int effectIndex = 0;",
                "effectIndex < static_cast<int>(StatisticsEffect::Count);",
                "ImGui::PushID(effectIndex);",
                "BeginAnimatedToggleRegion(",
                "\"##StatisticsTableRegion\"",
                "UiToggleRegionOwner::Performance",
                "drawStatisticsTable(",
                "EndAnimatedToggleRegion();",
                "ImGui::PopID();",
                "ImGui::PopStyleVar();"
            },
            "all timing-table choices stay submitted under stable keys so the "
            "old table rolls up while the new table rolls down without a "
            "transient sibling gap");
        Require(
            CountOccurrences(statistics, "case StatisticsEffect::") == 14u,
            "the keyed Performance table exchange must retain every timing "
            "effect.");

        const std::string_view performanceRoot = ExtractSection(
            viewer,
            "const float performanceWindowTop =",
            "const float settingsWindowTop =",
            "top-level Performance window");
        RequireOrdered(
            performanceRoot,
            {
                "ResolvePerformanceMaximumWindowHeight(",
                "ImVec2(settingsWindowWidth, performanceCollapsedHeight)",
                "ImGui::SetNextUvsrWindowCollapsedHeight(",
                "performanceCollapsedHeight);",
                "if (m_PerformanceCollapsedRequest)",
                "ImGui::SetNextUvsrWindowCollapseTarget(",
                "*m_PerformanceCollapsedRequest);",
                "ImGui::SetNextWindowCollapsed(",
                "*m_PerformanceCollapsedRequest,",
                "ImGuiCond_Always);",
                "m_PerformanceCollapsedRequest.reset();",
                "ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);",
                "const bool performanceExpanded = ImGui::Begin(",
                "\"Performance\"",
                "if (performanceExpanded)",
                "DrawPerformancePanelContents(",
                "ImGui::IsCurrentUvsrWindowCollapseTransitionActive()",
                "ImGui::End();"
            },
            "independently collapsible top-level Performance panel");
        RequireAbsent(
            performanceRoot,
            "m_SettingsCollapsed",
            "Performance collapse state independent of Settings");
        RequireContains(
            performanceRoot,
            "const bool performanceScrollIdle =",
            "detached Performance scroll-stability state");
        const std::string_view performanceCollapseSurface = ExtractSection(
            performanceRoot,
            "const float performanceCollapseRange =",
            "if (performanceExpanded)",
            "Performance collapse-only body opacity");
        RequireOrdered(
            performanceCollapseSurface,
            {
                "performanceWindow->SizeFull.y -",
                "performanceCollapsedHeight",
                "const bool performanceExpandedRangeKnown =",
                "performanceCollapseRange > 0.5f;",
                "const float performanceCollapseAmount =",
                "performanceExpandedRangeKnown",
                "? std::clamp(",
                "performanceWindow->SizeFull.y -",
                "performanceWindow->Size.y",
                "performanceWindow->Size.y <=",
                "performanceCollapsedHeight + 0.5f",
                "? 1.f",
                ": 0.f;",
                "if (performanceCollapseAmount > 0.f)",
                "GetOpaquePanelBodySurface();",
                "compactBodyOverlay.w = performanceCollapseAmount;",
                "performanceWindowDrawList->AddRectFilled(",
                "performanceBodyRect.Min,",
                "performanceBodyRect.Max,",
                "style.WindowRounding,",
                "ImDrawFlags_RoundCornersAll);"
            },
            "only Performance derives an opaque same-RGB body overlay from its "
            "live collapse size, including the first default-collapsed frame "
            "whose expanded range has not been measured yet");
        Require(
            CountOccurrences(viewer, "compactBodyOverlay.w = ") == 1u &&
                CountOccurrences(viewer, "performanceCollapseAmount") == 3u,
            "the collapse-specific opacity overlay must remain unique to the "
            "Performance root.");
        const std::string_view collapsedPerformance = ExtractSection(
            performanceRoot,
            "else\n        {\n            const ImVec2 summaryTextSize =",
            "DrawFilledRoundedInsetFrame(",
            "collapsed Performance summary endpoint");
        RequireOrdered(
            collapsedPerformance,
            {
                "const ImVec2 summaryTextSize = ImGui::CalcTextSize(",
                "performanceBodyRect.Min.y +",
                "(performanceBodyRect.GetHeight() -",
                "summaryTextSize.y) * 0.5f",
                "performanceWindowDrawList->PushClipRect(",
                "performanceWindowDrawList->AddText(",
                "performanceLine.c_str());",
                "performanceWindowDrawList->PopClipRect();"
            },
            "collapsed Performance directly paints its one-line summary beneath "
            "the title");
        for (const std::string_view expandedOnlyControl : {
                std::string_view("DrawPerformancePanelContents("),
                std::string_view("BeginRoundedCombo("),
                std::string_view("BeginTable(") })
        {
            RequireAbsent(
                collapsedPerformance,
                expandedOnlyControl,
                "collapsed Performance selector/table endpoint");
        }
        const std::string_view panelMinimumLayout = ExtractSection(
            viewer,
            "const float panelTitleMinimumHeight =",
            "m_CommandLayout = ResolveCommandInterfaceLayout(",
            "distinct root-panel collapsed heights");
        RequireOrdered(
            panelMinimumLayout,
            {
                "const float panelTitleMinimumHeight =",
                "GetSettingsCollapsedWindowHeight(",
                "const float performanceCollapsedHeight =",
                "panelTitleMinimumHeight +",
                "ImGui::GetStyle().WindowPadding.y * 2.f +",
                "ImGui::GetFontSize() +",
                "g_UiSpacingTokens.tight;",
                "const float minimumPanelStackHeight =",
                "performanceCollapsedHeight +",
                "panelSeparation +",
                "minimumSettingsHeight;"
            },
            "Performance reserves a summary row while Settings retains its "
            "independent title-only endpoint");
        RequireContains(
            Compact(viewer),
            "settingsScrollIdle&&performanceScrollIdle",
            "deferred UI actions wait for both root-panel scroll states");
        RequireContains(
            Compact(viewer),
            "!performanceCollapseTransitionActive&&"
            "!g_PerformanceTableTransitionActive",
            "deferred UI actions wait for the Performance table-height "
            "exchange");
        RequireOrdered(
            viewer,
            {
                "g_PerformanceAppearanceDrawLists.clear();",
                "g_PerformanceTableTransitionActive = false;",
                "ApplyWindowAppearance(\n"
                "            performanceWindowDrawList,",
                "for (ImDrawList* drawList :\n"
                "            g_PerformanceAppearanceDrawLists)",
                "ApplyWindowAppearance("
            },
            "Performance animation children receive the root appearance "
            "transform through their own per-frame draw-list set");

        const std::string_view panelStack = ExtractSection(
            viewer,
            "const float performanceWindowTop =",
            "ImDrawList* settingsBodyDrawList =",
            "Performance and Settings root hierarchy");
        RequireOrdered(
            panelStack,
            {
                "const bool performanceExpanded = ImGui::Begin(",
                "\"Performance\"",
                "const float settingsWindowTop =",
                "ImGui::Begin(\n            \"Settings\"",
                "ImGui::BeginChild(\n            \"##SettingsBody\""
            },
            "detached Performance panel before the Settings window");
        RequireContains(
            panelStack,
            "performanceWindowPosition.y +\n"
            "            performanceWindowSize.y +\n"
            "            panelSeparation;",
            "Performance-to-Settings boundary uses one ordinary drawer gap");

        const std::string_view settingsRoot = ExtractSection(
            viewer,
            "ImGui::Begin(\n            \"Settings\"",
            "ImDrawList* settingsBodyDrawList =",
            "Settings root without Performance content");
        RequireAbsent(
            settingsRoot,
            "ImGui::TextUnformatted(performanceLine.c_str())",
            "Performance summary detached from Settings");
        RequireAbsent(
            settingsRoot,
            "ogPerformanceLines[0].c_str()",
            "Ogg Performance first row detached from Settings");
        RequireAbsent(
            settingsRoot,
            "ogPerformanceLines[1].c_str()",
            "Ogg Performance second row detached from Settings");
        RequireAbsent(
            viewer,
            "\"Renderer: %s\"",
            "removed redundant Settings renderer identity");
        RequireAbsent(
            viewer,
            "rendererLine",
            "removed redundant Settings renderer buffer");
        RequireExactStrings(
            ExtractSection(
                statistics,
                "static constexpr const char* StatisticsEffectLabels[] = {",
                "};",
                "Statistics effect labels"),
            {
                "Complete Renderer",
                "Scene Setup",
                "Geometry",
                "Direct Lighting",
                "Screen Space Visibility",
                "Directional Shadows",
                "Temporal Reconstructive",
                "Fast Approximate",
                "Conservative Morphological",
                "Multisample Adaptive",
                "Material Picking",
                "Environment Background",
                "Tone Mapping",
                "Output Blit"
            },
            "Performance effect labels");
        const std::string_view statisticsEffectEnum = ExtractSection(
            viewer,
            "enum class StatisticsEffect : int",
            "struct UiVisualTokens",
            "Performance selector enum");
        RequireAbsent(
            statisticsEffectEnum,
            "Hardware",
            "removed Hardware Performance selector value");
        RequireContains(
            Compact(statistics),
            "static_assert(std::size(StatisticsEffectLabels)=="
            "static_cast<size_t>(StatisticsEffect::Count));",
            "Statistics selector enum coverage");
        RequireContains(
            statistics,
            "switch (effect)",
            "keyed single-effect Performance table renderer");
        RequireAbsent(
            statistics,
            "ImGui::TextUnformatted(\"View\")",
            "removed redundant Performance selector label");
        RequireOrdered(
            statistics,
            {
                "ImGui::PushItemWidth(settingsControlWidth);",
                "ImGui::SetCursorPosX(summaryCursorX);",
                "m_StatisticsEffect = std::clamp(",
                "if (BeginRoundedCombo(",
                "\"##StatisticsEffect\"",
                "ImGui::SetItemDefaultFocus()"
            },
            "unlabeled ordinary-width Performance selector");
        RequireAbsent(
            statistics,
            "ImGui::SetNextItemWidth(-FLT_MIN);",
            "Performance selector full-content-width override");
        RequireAbsent(
            statistics,
            "statisticsResetLaneWidth",
            "retired Performance selector reset lane");
        RequireAbsent(
            statistics,
            "DrawPresetResetIcon(",
            "retired same-row Performance selector reset icon");
        RequireOrdered(
            statistics,
            {
                "\"Complete Renderer Frame\"",
                "\"Scene Setup and Clears\"",
                "\"Geometry\"",
                "\"Closest Surface Resolve\"",
                "\"Shadow Ray Dispatch\"",
                "\"Shadow Denoise\"",
                "\"Sky Visibility Ray Dispatch\"",
                "\"Sky Visibility Denoise\"",
                "\"Direct Lighting\"",
                "\"Visibility Lighting Preparation\"",
                "\"Screen Space Visibility\"",
                "\"Ambient Occlusion Denoise\"",
                "\"Diffuse Illumination Denoise\"",
                "\"Material Picking\"",
                "\"Environment Background\"",
                "\"Auto Exposure\"",
                "\"Tone Mapping\"",
                "\"Fast Approximate\"",
                "\"Output Blit\""
            },
            "complete renderer timing table");
        RequireContains(
            Compact(statistics),
            "static_assert(std::size(CompleteRows)=="
            "static_cast<size_t>(RendererTimingStage::Count));",
            "complete renderer timing-stage coverage");
        for (const std::string_view table : {
                std::string_view("##CompleteRendererStatistics"),
                std::string_view("##SelectedRendererStatistics"),
                std::string_view("##DirectLightingStatistics"),
                std::string_view("##VisibilityStatistics"),
                std::string_view("##ShadowStatistics"),
                std::string_view("##TemporalStatistics"),
                std::string_view("##MorphologicalStatistics"),
                std::string_view("##MultisampleStatistics"),
                std::string_view("##ToneMappingStatistics") })
        {
            RequireContains(statistics, table, "detailed Statistics table");
        }
        for (const std::string_view removedHardwareSurface : {
                std::string_view("case StatisticsEffect::Hardware"),
                std::string_view("##HardwareStatistics"),
                std::string_view("m_HardwareCapabilities"),
                std::string_view("hardware specifications"),
                std::string_view("\"RAM Speed\""),
                std::string_view("\"VRAM Speed\""),
                std::string_view("\"GPU TFLOPS\""),
                std::string_view("\"CPU TFLOPS\"") })
        {
            RequireAbsent(
                statistics,
                removedHardwareSurface,
                "removed Hardware Performance view");
        }
        RequireContains(
            statistics,
            "ImGui::TableHeadersRow();",
            "Statistics table headers");
        RequireContains(
            Compact(statistics),
            "ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg|"
            "ImGuiTableFlags_SizingStretchProp;",
            "striped Statistics table contract");
        const std::string_view statisticsRowHelper = ExtractSection(
            statistics,
            "const auto beginStatisticsRow =",
            "const auto drawMilliseconds =",
            "Performance row helper");
        RequireContains(
            statisticsRowHelper,
            "ImGui::TableNextRow();",
            "Performance row helper advances the table only when invoked");
        const std::string_view millisecondRows = ExtractSection(
            statistics,
            "const auto drawMilliseconds =",
            "const auto drawCount =",
            "Performance millisecond rows");
        RequireOrdered(
            millisecondRows,
            {
                "m_PerformanceTimingRows.Resolve(",
                "performanceTimingViewId,",
                "ImHashStr(label),",
                "value,",
                "available);",
                "if (!rowState.IsVisible())",
                "return;",
                "beginStatisticsRow(label, rowState.HasMeasurement());",
                "if (rowState.HasMeasurement())",
                "ImGui::Text(\"%.3f ms\", rowState.milliseconds);",
                "else",
                "ImGui::TextDisabled(\"--\");"
            },
            "a timing row appears after its first real measurement, remains "
            "stable with a disabled placeholder, and treats unavailable data "
            "as zero");
        RequireContains(
            viewer,
            "uvsr::PerformanceTimingRowRetention m_PerformanceTimingRows;",
            "session-owned Performance timing-row retention state");
        RequireOrdered(
            statistics,
            {
                "const auto drawStatisticsTable =",
                "performanceTimingViewId = static_cast<uint32_t>(effect);",
                "switch (effect)"
            },
            "Performance timing retention isolates identical row labels by "
            "selected view");
        const std::string_view sceneLoadingReset = ExtractSection(
            viewer,
            "const bool sceneLoading = m_app->IsSceneBusy();",
            "BeginFullScreenWindow();",
            "scene-loading Statistics reset");
        RequireAbsent(
            sceneLoadingReset,
            "m_PerformanceTimingRows",
            "scene loading must not forget once-observed Performance rows");
        const std::vector<std::string_view> nonTimeRows = {
            ExtractSection(
                statistics,
                "const auto drawCount =",
                "const auto drawMemory =",
                "Performance count rows"),
            ExtractSection(
                statistics,
                "const auto drawMemory =",
                "const auto drawText =",
                "Performance memory rows"),
            ExtractSection(
                statistics,
                "const auto drawText =",
                "const auto drawRendererTiming =",
                "Performance text rows")
        };
        for (const std::string_view nonTimeRow : nonTimeRows)
        {
            RequireOrdered(
                nonTimeRow,
                {
                    "beginStatisticsRow(label, available);",
                    "if (available)",
                    "else",
                    "ImGui::TextDisabled(\"--\");"
                },
                "unavailable non-time Performance row remains visible");
            RequireAbsent(
                nonTimeRow,
                "if (!available)",
                "non-time Performance rows must not be elided");
        }
        RequireContains(
            statistics,
            "\"Minimum History Formats\", available",
            "non-time temporal format row remains visible when unavailable");
        RequireContains(
            statistics,
            "timings.IsAvailable(stage)",
            "renderer timing availability gate");
        RequireContains(
            viewer,
            "m_RendererTimings.available[stageIndex] = false;",
            "dormant renderer timing invalidation");
        RequireContains(
            viewer,
            "m_RendererTimings.available[stageIndex] = currentEpoch;",
            "epoch-validated renderer timing publication");
        RequireContains(
            viewer,
            "timings->active && timings->available",
            "completed-query effect timing gate");
        RequireContains(
            viewer,
            "timings->dispatchCount > 0u &&\n                timings->available",
            "completed temporal timing gate");
        RequireContains(
            viewer,
            "snapshot.hasCmaa2Timings = timings->available;",
            "completed morphological timing gate");
        RequireContains(
            viewer,
            "m_ui.AntiAliasing.cmaa2.enabled",
            "disabled morphological timing gate");
        RequireContains(
            statistics,
            "RendererTimingStage::FastApproximate",
            "Fast Approximate renderer timing gate");
        RequireContains(
            statistics,
            "RendererTimingStage::VisibilityLightingPreparation",
            "MSAA Visibility lighting-preparation timing row");
        const std::string compactStatistics = Compact(statistics);
        for (const std::string_view splitTiming : {
                std::string_view(
                    "{\"ShadowRayDispatch\","
                    "RendererTimingStage::ShadowRayDispatch}"),
                std::string_view(
                    "{\"ShadowDenoise\","
                    "RendererTimingStage::ShadowDenoise}"),
                std::string_view(
                    "{\"SkyVisibilityRayDispatch\","
                    "RendererTimingStage::SkyVisibilityRayDispatch}"),
                std::string_view(
                    "{\"SkyVisibilityDenoise\","
                    "RendererTimingStage::SkyVisibilityDenoise}"),
                std::string_view(
                    "{\"AmbientOcclusionDenoise\","
                    "RendererTimingStage::AmbientOcclusionDenoise}"),
                std::string_view(
                    "{\"DiffuseIlluminationDenoise\","
                    "RendererTimingStage::DiffuseIlluminationDenoise}") })
        {
            RequireContains(
                compactStatistics,
                splitTiming,
                "separate ray-dispatch and denoising Statistics row");
        }
        RequireAbsent(
            statistics,
            "Ray Traced Shadow Dispatch",
            "renamed Shadow Ray Dispatch Statistics row");
        RequireContains(
            viewer,
            "m_DisplayedTemporalAATimings = snapshot.temporalAATimings;",
            "complete temporal timing snapshot retention");
        RequireContains(
            statistics,
            "\"Presentation Sharpen\"",
            "deferred temporal sharpening timing");
        RequireContains(
            statistics,
            "m_HasCmaa2StatSnapshot",
            "morphological cost breakdown");
        RequireContains(
            statistics,
            "\"Geometry\", RendererTimingStage::Geometry",
            "multisample geometry and lighting breakdown");
        RequireContains(
            statistics,
            "\"Closest Surface Resolve\"",
            "multisample closest-surface breakdown");
        RequireContains(
            statistics,
            "m_app->GetActiveRasterSampleCount()",
            "active multisample topology reporting");
        RequireContains(
            statistics,
            "const bool active = enabled && activeSamples > 1u;",
            "unsupported multisample timing rejection");
        const std::string_view multisampleStatistics = ExtractSection(
            statistics,
            "case StatisticsEffect::Multisample:",
            "case StatisticsEffect::MaterialPicking:",
            "Multisample Performance table");
        RequireOrdered(
            multisampleStatistics,
            {
                "drawText(",
                "\"Status\"",
                "drawCount(",
                "\"Requested Samples\"",
                "drawCount(\"Active Samples\"",
                "\"Geometry\",",
                "RendererTimingStage::Geometry,",
                "active);",
                "\"Direct Lighting\"",
                "active);",
                "\"Visibility Lighting Preparation\"",
                "active);",
                "\"Closest Surface Resolve\"",
                "active);",
                "\"Complete Renderer Frame\""
            },
            "Multisample submits every retained timing identity each frame and "
            "uses active topology only as current-measurement eligibility");
        Require(
            CountOccurrences(
                multisampleStatistics,
                "drawRendererTiming(") == 5u,
            "Multisample must retain four topology-eligible effect timings plus the "
            "complete-frame timing.");
        RequireAbsent(
            multisampleStatistics,
            "drawMilliseconds(",
            "removed Multisample placeholder timing rows");
        for (const std::string_view retainedBreakdown : {
                std::string_view("First Trace"),
                std::string_view("Reconstruction"),
                std::string_view("Dispatches"),
                std::string_view("Active History Memory"),
                std::string_view("History Status"),
                std::string_view("Minimum History Formats"),
                std::string_view("Candidate Processing") })
        {
            RequireContains(
                statistics,
                retainedBreakdown,
                "retained effect cost breakdown");
        }
        for (const std::string_view retiredStatistics : {
                std::string_view("Sparse Virtual Shadow"),
                std::string_view("Cascaded Shadow"),
                std::string_view("VisibilityPerformance"),
                std::string_view("Benchmark"),
                std::string_view("Export") })
        {
            RequireAbsent(
                statistics,
                retiredStatistics,
                "retired Statistics backend surface");
        }
    }

    void ValidateTemporalTiming(std::string_view temporalPass)
    {
        RequireContains(
            temporalPass,
            "m_TimerSubmissionSlot = slot;",
            "shared temporal timing slot");
        RequireContains(
            temporalPass,
            "m_TimerFrameWritable && m_TimerHasSubmission[slot]",
            "complete temporal timing publication");
        RequireOrdered(
            temporalPass,
            {
                "Temporal AA Presentation Sharpen",
                "++m_Timings.dispatchCount;",
                "EndStage(commandList, Stage::PresentationSharpen);"
            },
            "deferred temporal sharpening timing and dispatch count");
    }

    void ValidateAntiAliasing(
        std::string_view viewer,
        std::string_view temporalOptions)
    {
        const std::string_view aliasing = ExtractSection(
            viewer,
            "const bool antiAliasingOpen = DrawCollapsingHeader(",
            "const bool debugOpen = DrawCollapsingHeader(",
            "Aliasing drawer");
        RequireOrdered(
            aliasing,
            {
                "\"Temporal Reconstructive##Aliasing\"",
                "\"Enable##TemporalReconstructive\"",
                "\"Quality##TemporalReconstructive\"",
                "ImGui::SetNextItemOpen(false, ImGuiCond_Once);",
                "\"Advanced##TemporalReconstructive\"",
                "\"Jitter Sequence##TemporalReconstructive\"",
                "\"Depth Validation\"",
                "\"History Strength\"",
                "ImGui::SetNextItemOpen(false, ImGuiCond_Once);",
                "\"Cost##TemporalAdvancedCost\"",
                "\"Mode##TemporalCost\"",
                "\"History Storage\"",
                "\"Output Sharpening\"",
                "\"Fast Approximate##Aliasing\"",
                "\"Enable##FastApproximate\"",
                "\"Quality##FastApproximate\"",
                "\"Advanced##FastApproximate\"",
                "\"Edge Sharpness##FastApproximate\"",
                "\"Relative Edge Threshold##FastApproximate\"",
                "\"Minimum Edge Threshold##FastApproximate\"",
                "\"Conservative Morphological##Aliasing\"",
                "\"Enable##ConservativeMorphological\"",
                "\"Quality##ConservativeMorphological\"",
                "\"Advanced##ConservativeMorphological\"",
                "\"Edge Threshold##ConservativeMorphological\"",
                "\"Detector##ConservativeMorphological\"",
                "\"Multisample Adaptive##Aliasing\"",
                "\"Enable##MultisampleAdaptive\"",
                "\"Quality##MultisampleAdaptive\"",
                "\"Advanced##MultisampleAdaptive\"",
                "\"Samples##MultisampleAdaptive\""
            },
            "independent Aliasing controls");
        RequireContains(
            aliasing,
            "const bool temporalQualityCustom =",
            "Algorithm-owned Quality custom state");
        RequireContains(
            aliasing,
            "aliasing.temporal.stationaryBypass !=\n"
            "                    aliasingDefaults.temporal.stationaryBypass ||",
            "Depth Validation Quality ownership");
        RequireContains(
            aliasing,
            "!(aliasing.temporal.algorithmOverrides ==\n"
            "                    aliasingDefaults.temporal.algorithmOverrides)",
            "Algorithm override Quality ownership");
        RequireContains(
            aliasing,
            "const bool temporalCostCustom =",
            "Cost-owned custom state");
        RequireContains(
            aliasing,
            "!(aliasing.temporal.behaviorOverrides ==\n"
            "                    aliasingDefaults.temporal.behaviorOverrides)",
            "Cost override ownership");
        RequireContains(
            aliasing,
            "m_ui.TemporalAaSharpenEnabled ||\n"
            "                m_ui.TemporalAaSharpness != "
            "TemporalAaDefaultSharpness",
            "output-sharpening Cost ownership");
        RequireContains(
            aliasing,
            "preview += \" (Custom)\";",
            "preset preview Custom suffix");
        RequireContains(
            aliasing,
            "const bool isSelected =\n"
            "                            !custom && selected == index;",
            "Custom preset row remains re-applicable");
        RequireContains(
            aliasing,
            "applySelection(\n"
            "                                    static_cast<Value>(index));",
            "preset group application callback");
        Require(
            CountOccurrences(aliasing, "\" (Custom)\"") == 1u,
            "Aliasing must append Custom only through the shared preset "
            "preview path.");
        RequireExactStrings(
            ExtractSection(
                aliasing,
                "static constexpr const char* QualityLabels[] = {",
                "};",
                "Aliasing quality labels"),
            { "Low", "Medium", "High", "Ultra" },
            "Aliasing quality labels");
        RequireExactStrings(
            ExtractSection(
                aliasing,
                "static constexpr const char* CostLabels[] = {",
                "};",
                "Cost labels"),
            { "Full Quality", "Reduced", "Minimum" },
            "Cost labels");
        RequireExactStrings(
            ExtractSection(
                aliasing,
                "static constexpr const char* JitterSequenceLabels[] = {",
                "};",
                "Jitter Sequence labels"),
            {
                "Rotated Grid 4",
                "Uniform Helix 4",
                "Halton 8",
                "Halton 16",
                "Halton 32",
                "Sobol 32"
            },
            "Jitter Sequence labels");
        RequireContains(
            aliasing,
            "\"TemporalJitterSequence\"",
            "Jitter Sequence dedicated reset");
        RequireContains(
            aliasing,
            "settings->temporal.jitterSequence =\n"
            "                                    static_cast<TemporalAaJitterSequence>(",
            "Jitter Sequence independent selection callback");
        RequireContains(
            aliasing,
            "Choose Quality. Algorithm changes append (Custom); the arrow \"\n"
            "                \"restores factory Quality and its Algorithm controls.",
            "Quality tooltip excludes independent Jitter Sequence ownership");
        RequireContains(
            aliasing,
            "temporalQualityCustom,\n"
            "                applyTemporalQualityPreset);",
            "Quality custom preview and group binding");
        RequireContains(
            Compact(aliasing),
            "temporalCostCustom,applyTemporalCostPreset);",
            "Cost custom preview and group binding");
        RequireContains(
            aliasing,
            "aliasingDefaults.temporal.quality ||\n"
            "                    temporalQualityCustom",
            "Quality group reset visibility");
        RequireContains(
            aliasing,
            "settings->temporal.stationaryBypass =\n"
            "                        stationaryBypass;",
            "Quality group Depth Validation reset");
        RequireContains(
            aliasing,
            "settings->temporal.algorithmOverrides =\n"
            "                        algorithmOverrides;",
            "Quality group Algorithm reset");
        RequireContains(
            Compact(aliasing),
            "aliasingDefaults.temporal.costMode||temporalCostCustom",
            "Cost group reset visibility");
        RequireContains(
            aliasing,
            "settings->temporal.behaviorOverrides =\n"
            "                        behaviorOverrides;",
            "Cost group policy reset");
        RequireContains(
            aliasing,
            "ui->TemporalAaSharpenEnabled = false;\n"
            "                    ui->TemporalAaSharpness =\n"
            "                        TemporalAaDefaultSharpness;",
            "Cost group sharpening reset");

        Require(
            CountOccurrences(aliasing, "drawPresetEnum(") == 5u,
            "Aliasing must expose four shared Quality rows and the nested "
            "Temporal Cost mode; Jitter Sequence remains independent in "
            "Advanced.");
        for (const std::string_view recipeContract : {
                std::string_view("MatchesFastApproximateAaQualityPreset"),
                std::string_view("ApplyFastApproximateAaQualityPreset"),
                std::string_view("MatchesCmaa2QualityPreset"),
                std::string_view("ApplyCmaa2QualityPreset"),
                std::string_view("MatchesMultisampleQualityPreset"),
                std::string_view("ApplyMultisampleQualityPreset") })
        {
            RequireContains(
                aliasing,
                recipeContract,
                "spatial AA Quality recipe wiring");
        }

        const std::string_view cmaa2Advanced = ExtractSection(
            aliasing,
            "\"Advanced##ConservativeMorphological\"",
            "\"Multisample Adaptive##Aliasing\"",
            "CMAA2 Advanced section");
        RequireOrdered(
            cmaa2Advanced,
            {
                "\"Edge Threshold##ConservativeMorphological\"",
                "\"Detector##ConservativeMorphological\""
            },
            "CMAA2 Advanced controls");
        RequireExactStrings(
            ExtractSection(
                cmaa2Advanced,
                "static constexpr const char* DetectorLabels[] = {",
                "};",
                "CMAA2 detector labels"),
            { "Luma", "Full Color" },
            "CMAA2 detector labels");
        RequireAbsent(
            cmaa2Advanced,
            "Quality##ConservativeMorphological",
            "CMAA2 Advanced section");

        const std::string_view advanced = ExtractSection(
            aliasing,
            "\"Advanced##TemporalReconstructive\"",
            "\"Fast Approximate##Aliasing\"",
            "Temporal Advanced section");
        for (const std::string_view control : {
                std::string_view("\"Jitter Sequence##TemporalReconstructive\""),
                std::string_view("\"Depth Validation\""),
                std::string_view("\"Motion Source\""),
                std::string_view("\"Current Sample\""),
                std::string_view("\"History Filter\""),
                std::string_view("\"Rectification\""),
                std::string_view("\"History Frames\""),
                std::string_view("\"History Strength\""),
                std::string_view("\"History Storage\""),
                std::string_view("\"History Weight\""),
                std::string_view("\"Motion Trust\""),
                std::string_view("\"Rectification Clip\""),
                std::string_view("\"Blend Domain\""),
                std::string_view("\"Preset Sharpening\""),
                std::string_view("\"Output Sharpening\"") })
        {
            RequireContains(advanced, control, "Temporal Advanced control");
        }
        RequireAbsent(
            advanced,
            "drawEnum(\n                \"Temporal Cost\"",
            "Temporal Advanced section");
        RequireContains(
            advanced,
            "ResolveAntiAliasingSettings(inheritedAliasing)",
            "resolved inherited Advanced previews");
        RequireOrdered(
            advanced,
            {
                "\"Jitter Sequence##TemporalReconstructive\"",
                "\"Depth Validation\"",
                "\"Motion Source\"",
                "\"History Strength\"",
                "ImGui::SetNextItemOpen(false, ImGuiCond_Once);",
                "\"Cost##TemporalAdvancedCost\"",
                "ImGuiTreeNodeFlags_None",
                "\"Mode##TemporalCost\"",
                "\"History Storage\"",
                "\"Output Sharpening\"",
                "EndAnimatedTreeNode();",
                "EndAnimatedTreeNode();"
            },
            "Temporal Advanced directly presents algorithm controls, then "
            "ends with one initially closed Cost foldout");
        RequireAbsent(
            advanced,
            "ImGui::SeparatorText(\"Algorithm\")",
            "removed redundant Temporal Algorithm separator");
        RequireAbsent(
            advanced,
            "ImGui::SeparatorText(\"Cost\")",
            "Cost uses an animated foldout instead of a separator");
        Require(
            CountOccurrences(
                advanced,
                "\"Cost##TemporalAdvancedCost\"") == 1u &&
                CountOccurrences(
                    advanced,
                    "\"Mode##TemporalCost\"") == 1u,
            "Temporal Advanced must retain one stable Cost foldout and one "
            "stable nested Mode control ID.");
        RequireContains(
            Compact(advanced),
            "constintselectedIndex=std::clamp("
            "selectedValue==0?inheritedIndex:selectedValue-1,0,count-1);",
            "resolved concrete Advanced preview mapping");
        RequireContains(
            advanced,
            "const int optionValue = index + 1;",
            "sentinel-safe explicit Advanced option mapping");
        RequireContains(
            advanced,
            "const bool useInherited =\n"
            "                                !inheritedOption &&\n"
            "                                index == inheritedIndex;",
            "preset-equivalent Advanced option reconciliation");
        RequireContains(
            advanced,
            "(!useInherited &&\n"
            "                                    selectedValue == optionValue)",
            "explicit preset-equivalent option remains re-applicable");
        RequireContains(
            advanced,
            "useInherited\n"
            "                                            ? 0\n"
            "                                            : optionValue",
            "Advanced inherited sentinel restoration");
        RequireContains(
            advanced,
            "DrawNestedDropdownResetIcon(",
            "Advanced inheritance reset icon");
        RequireAbsent(
            advanced,
            "inheritedLabel",
            "Advanced parenthetical owner labels");
        RequireAbsent(
            advanced,
            "\"Profile\"",
            "Advanced Profile provenance option");
        RequireAbsent(
            advanced,
            "\"Temporal Cost\"",
            "Advanced Temporal Cost provenance option");
        RequireAbsent(
            advanced,
            "ImGui::SeparatorText(\"Behavior\")",
            "retired Temporal Behavior heading");
        RequireContains(
            advanced,
            "\" (Automatic)\"",
            "retained Automatic provenance option");
        RequireContains(
            advanced,
            "&historyFrames,\n"
                "                        1,\n"
                "                        32,\n"
                "                        1,\n"
                "                        16",
            "logical 32-frame limit with a practical 16-frame travel cap");
        RequireContains(
            advanced,
            "0.f,\n                    200.f,",
            "intuitive history-strength percentage range");
        RequireContains(
            viewer,
            "requested != -1 && (requested < 1 || requested > 32)",
            "history-frame command rejects zero");
        RequireContains(
            viewer,
            "requested != -1.f &&\n                        (requested < 0.f || requested > 2.f)",
            "history-strength command rejects sentinel-adjacent values");
        Require(
            CountOccurrences(aliasing, "BeginAnimatedTreeNode(") == 9u,
            "Aliasing must expose four technique disclosures, four Advanced "
            "disclosures, and the nested Temporal Cost disclosure.");
        Require(
            CountOccurrences(
                aliasing,
                "ImGui::SetNextItemOpen(false, ImGuiCond_Once);") == 5u,
            "every Aliasing Advanced disclosure and nested Temporal Cost "
            "disclosure must start collapsed.");

        const std::string_view temporalSettings = ExtractSection(
            temporalOptions,
            "struct TemporalAaSettings",
            "struct FastApproximateAaSettings",
            "Temporal AA defaults");
        const std::string_view fastApproximateSettings = ExtractSection(
            temporalOptions,
            "struct FastApproximateAaSettings",
            "struct Cmaa2Settings",
            "Fast Approximate defaults");
        const std::string_view cmaa2Settings = ExtractSection(
            temporalOptions,
            "struct Cmaa2Settings",
            "struct MsaaSettings",
            "CMAA2 defaults");
        const std::string_view msaaSettings = ExtractSection(
            temporalOptions,
            "struct MsaaSettings",
            "struct AntiAliasingSettings",
            "MSAA defaults");
        RequireContains(
            Compact(temporalSettings),
            "boolenabled=false;",
            "TAA default");
        RequireContains(
            Compact(fastApproximateSettings),
            "boolenabled=false;",
            "Fast Approximate default");
        RequireContains(
            Compact(fastApproximateSettings),
            "edgeSharpness=FastApproximateAaDefaultEdgeSharpness;",
            "Fast Approximate sharpness default");
        RequireContains(
            Compact(fastApproximateSettings),
            "AntiAliasingQualityquality=AntiAliasingQuality::Ultra;",
            "Fast Approximate Quality default");
        RequireContains(
            Compact(cmaa2Settings),
            "boolenabled=false;",
            "CMAA2 default");
        RequireContains(
            Compact(cmaa2Settings),
            "edgeThreshold=Cmaa2DefaultEdgeThreshold;",
            "CMAA2 threshold default");
        RequireContains(
            Compact(cmaa2Settings),
            "Cmaa2EdgeDetectordetector=Cmaa2EdgeDetector::FullColor;",
            "CMAA2 detector default");
        RequireContains(
            Compact(msaaSettings),
            "boolenabled=false;",
            "MSAA default");
        RequireContains(
            Compact(msaaSettings),
            "AntiAliasingQualityquality=AntiAliasingQuality::Medium;",
            "Multisample Adaptive Quality default");
        RequireContains(
            Compact(temporalSettings),
            "boolstationaryBypass=true;",
            "Stationary Bypass default");
        for (const std::string_view retired : {
                std::string_view("AntiAliasingMethod"),
                std::string_view("Sample Resurrection"),
                std::string_view("SampleResurrection"),
                std::string_view("HDR CMAA2"),
                std::string_view("PresentSelectors("),
                std::string_view("PresentStructuralBody(") })
        {
            RequireAbsent(aliasing, retired, "cleaned Aliasing drawer");
        }
    }

    void ValidateDebug(std::string_view viewer)
    {
        const std::string_view debug = ExtractSection(
            viewer,
            "const bool debugOpen = DrawCollapsingHeader(",
            "const bool skyOpen = DrawCollapsingHeader(",
            "Debug drawer");
        RequireOrdered(
            debug,
            {
                "\"World##Debug\"",
                "\"Visibility##Debug\"",
                "\"Physically Based Lighting##Debug\""
            },
            "effect-grouped Debug drawer");

        RequireExactStrings(
            ExtractSection(
                debug,
                "static constexpr const char* WorldLabels[] = {",
                "};",
            "world appearance labels"),
            {
                "Default",
                "White",
                "White Detail",
                "White Lighting"
            },
            "world appearance labels");
        RequireExactStrings(
            ExtractSection(
                debug,
                "static constexpr const char* VisibilityDebugLabels[] = {",
                "};",
                "Visibility information-filter labels"),
            {
                "Default",
                "Ambient Visibility",
                "Traced Indirect",
                "Applied Indirect"
            },
            "Visibility information-filter labels");
        RequireExactStrings(
            ExtractSection(
                debug,
                "static constexpr const char* LightingLabels[] = {",
                "};",
                "PBR information-filter labels"),
            {
                "Default",
                "Surface Normals",
                "Geometry Normals",
                "Normal Difference",
                "Diffuse Environment",
                "Environment Direction",
                "Reflected Environment",
                "Reflectance Response",
                "Specular Environment",
                "All Environment Light",
                "Specular Visibility",
                "Environment Level"
            },
            "PBR information-filter labels");
        for (const std::string_view state : {
                std::string_view("m_ui.WhiteWorld"),
                std::string_view(
                    "m_ui.ScreenSpaceVisibility.debugView"),
                std::string_view("m_ui.LightingDebugView") })
        {
            RequireContains(debug, state, "composable Debug state");
        }
        Require(
            CountOccurrences(debug, "BeginAnimatedTreeNode(") == 3u,
            "Debug effects must retain three animated disclosures.");
        Require(
            CountOccurrences(debug, "BeginRoundedCombo(") == 3u &&
                CountOccurrences(
                    debug,
                    "DrawDeferredDropdownOption(") == 3u &&
                CountOccurrences(debug, "ImGui::EndCombo();") == 3u,
            "Debug effects must use exactly three deferred rounded combos.");
        Require(
            CountOccurrences(debug, "for (int index = 0;") == 3u &&
                CountOccurrences(debug, "[this, candidate]()") == 3u,
            "each Debug combo must enumerate candidates and capture the typed "
            "candidate by value for deferred application.");
        RequireAbsent(
            debug,
            "ImGui::Combo(",
            "raw immediate Debug combo mutation");
        Require(
            CountOccurrences(debug, "ImGuiTreeNodeFlags_DefaultOpen") == 4u,
            "Debug and all three effect groups must start expanded.");
        RequireContains(
            debug,
            "every effect-specific debug view.",
            "world and information-filter composability guidance");
        RequireContains(
            debug,
            "Visibility remains enabled and can still be inspected.",
            "lighting and Visibility composability guidance");
        for (const std::string_view removedOverlaySurface : {
                std::string_view("Edge Overlay"),
                std::string_view("Overlay Opacity"),
                std::string_view("edgeOverlay"),
                std::string_view("DebugOverlayOpacity"),
                std::string_view("Screen-Space Shadows##Debug") })
        {
            RequireAbsent(
                debug,
                removedOverlaySurface,
                "isolation-only shadow Debug controls");
        }
    }

    void ValidateVisibilityPbrDecoupling(std::string_view viewer)
    {
        const std::string_view uiData = ExtractSection(
            viewer,
            "struct UIData",
            "enum class RendererTimingStage",
            "UI renderer state");
        const std::string_view visibilityDrawer = ExtractSection(
            viewer,
            "const bool diffuseOpen = DrawCollapsingHeader(",
            "const bool denoisingOpen = DrawCollapsingHeader(",
            "Diffuse drawer");
        const std::string_view visibilityDispatcher = ExtractSection(
            viewer,
            "bool DispatchVisibilityCommandValue(",
            "bool DispatchAliasingCommandValue(",
            "Visibility command dispatcher");

        RequireContains(
            uiData,
            "ScreenSpaceVisibilitySettings       ScreenSpaceVisibility;",
            "independent Visibility state");
        RequireContains(
            uiData,
            "AntiAliasingSettings                AntiAliasing;",
            "independent Anti-Aliasing state");
        RequireContains(
            viewer,
            "std::unique_ptr<PbrDeferredLightingPass> m_PbrDeferredLightingPass;",
            "singular deferred PBR path");
        RequireContains(
            Compact(viewer),
            "constboolscreenSpaceVisibilityRequested="
            "m_ui.HasActiveScreenSpaceVisibilityConsumer();",
            "Visibility runtime independence from lighting Debug filters");
        const std::string_view visibilityConsumer = ExtractSection(
            viewer,
            "bool HasActiveScreenSpaceVisibilityConsumer() const",
            "ResolvedAntiAliasingSettings",
            "Visibility consumer gate");
        RequireContains(
            visibilityConsumer,
            "HasActiveScreenSpaceVisibilityDebugConsumer()",
            "Visibility debug consumer");
        RequireContains(
            visibilityConsumer,
            "ScreenSpaceVisibility.HasActiveConsumer()",
            "Visibility debug effect gate");
        RequireContains(
            visibilityConsumer,
            "VisibilityDebugView::FinalImage",
            "Visibility debug final-image gate");
        for (const std::string_view retiredCoupling : {
                std::string_view("EnablePbr"),
                std::string_view("RendererMode::Forward"),
                std::string_view("m_ForwardPass") })
        {
            RequireAbsent(viewer, retiredCoupling, "Visibility/PBR decoupling");
        }
        for (const std::string_view unrelatedState : {
                std::string_view("LightingDebugView"),
                std::string_view("WhiteWorld"),
                std::string_view("AntiAliasing") })
        {
            RequireAbsent(
                visibilityDrawer,
                unrelatedState,
                "Visibility drawer decoupling");
            RequireAbsent(
                visibilityDispatcher,
                unrelatedState,
                "Visibility command decoupling");
        }
    }

    void ValidateRayTracedShadows(
        std::string_view viewer,
        std::string_view catalog)
    {
        const std::string_view shadows = ExtractSection(
            viewer,
            "const bool shadowsOpen = DrawCollapsingHeader(",
            "constexpr float ActionButtonCount = 4.f;",
            "Shadows drawer");
        const std::string_view shadowControls = ExtractSection(
            viewer,
            "const auto drawRatioEstimatorShadowControls = [&]()",
            "const bool diffuseOpen = DrawCollapsingHeader(",
            "Ray Traced Shadows controls");
        RequireContains(
            shadowControls,
            "Ray Traced Shadows##Shadows",
            "public Ray Traced Shadows name");
        RequireContains(
            shadows,
            "m_app->HasPrimaryDirectionalLight()",
            "directional-light availability gate");
        RequireContains(
            shadowControls,
            "Enabled##RatioEstimatorShadows",
            "ray traced shadow enable control");
        RequireAbsent(
            shadows,
            "Technique##DirectionalShadows",
            "retired exclusive directional-shadow selector");
        for (const std::string_view control : {
                std::string_view("Ratio Estimator##RatioEstimatorShadows"),
                std::string_view(
                    "Output Hit Distance##RatioEstimatorShadows"),
                std::string_view("Hard Shadows##RatioEstimatorShadows"),
                std::string_view("Samples Per Pixel##RatioEstimatorShadows"),
                std::string_view("Specify Noise##RatioEstimatorShadows"),
                std::string_view("Max Distance##RatioEstimatorShadows"),
                std::string_view("Ray Bias##RatioEstimatorShadows") })
        {
            RequireContains(
                shadowControls, control, "ray traced shadow control");
        }
        RequireOrdered(
            shadowControls,
            {
                "Ratio Estimator##RatioEstimatorShadows",
                "Output Hit Distance##RatioEstimatorShadows",
                "Samples Per Pixel##RatioEstimatorShadows",
                "Specify Noise##RatioEstimatorShadows",
                "##RatioEstimatorShadowCustomNoise",
                "Max Distance##RatioEstimatorShadows"
            },
            "ray traced shadow control order");
        RequireOrdered(
            Compact(shadowControls),
            {
                "ResolveNoiseSettings(m_ui.Noise,ratio.noise)",
                "&ratio.noise.specifyNoise",
                "ratio.noise.custom,",
                "ratioDefaults.noise.custom,",
                "ResetNoiseSamplingHistory(false,true,false,false)"
            },
            "ray traced shadow custom noise isolation");
        RequireContains(
            shadows,
            "drawRatioEstimatorShadowControls();",
            "ray traced shadow control placement");
        for (const std::string_view removedControl : {
                std::string_view("Denoiser Radius##RatioEstimatorShadows"),
                std::string_view("Origin Safety##RatioEstimatorShadows"),
                std::string_view("Noise Response##RatioEstimatorShadows"),
                std::string_view("Plane Rejection##RatioEstimatorShadows"),
                std::string_view("Normal Rejection##RatioEstimatorShadows"),
                std::string_view("Analytic Rejection##RatioEstimatorShadows") })
        {
            RequireAbsent(viewer, removedControl, "removed Heitz denoiser control");
        }
        RequireAbsent(shadows, "\"Isolation View\"", "Shadows drawer");
        RequireAbsent(shadows, "\"Edge Overlay\"", "Shadows drawer");
        RequireAbsent(
            viewer,
            "Screen-Space Directional Shadows",
            "quarantined screen space directional shadows");
        RequireAbsent(
            viewer,
            "Ratio Estimate Ray Traced Shadows",
            "retired public shadow name");
        for (const std::string_view path : {
                std::string_view("shadows.ray-traced.enabled"),
                std::string_view("shadows.ray-traced.ratio-estimator"),
                std::string_view("shadows.ray-traced.output-hit-distance"),
                std::string_view("shadows.ray-traced.samples-per-pixel"),
                std::string_view("shadows.ray-traced.specify-noise"),
                std::string_view("shadows.ray-traced.noise-pattern"),
                std::string_view("shadows.ray-traced.noise-resolution"),
                std::string_view("shadows.ray-traced.animate-samples") })
        {
            RequireContains(catalog, path, "Ray Traced Shadows command");
        }
        RequireAbsent(
            catalog,
            "shadows.ratio-estimator",
            "retired shadow command prefix");
        RequireAbsent(
            catalog,
            "shadows.screen-space-directional",
            "quarantined screen space shadow commands");

        for (const std::string_view retiredTechnique : {
                std::string_view("SparseVirtualShadowMap"),
                std::string_view("Sparse Virtual Shadow Maps"),
                std::string_view("DiagnosticCascadedShadowMap"),
                std::string_view("Diagnostic Cascaded Shadow Maps"),
                std::string_view("shadows.svsm"),
                std::string_view("shadows.csm") })
        {
            RequireAbsent(viewer, retiredTechnique, "retired shadow engine");
            RequireAbsent(catalog, retiredTechnique, "retired shadow commands");
        }
    }

    void ValidateCatalogAndDispatch(
        std::string_view viewer,
        std::string_view catalogSource)
    {
        const std::string_view factoryReset = ExtractSection(
            viewer,
            "void ResetAllSettingsToFactoryDefaults()",
            "bool DispatchGeneralCommandValue(",
            "renderer and Interface factory reset");
        RequireOrdered(
            factoryReset,
            {
                "m_app->ResetAllRendererSettings();",
                "ApplyAdaptiveSyncMode(GetDefaultAdaptiveSyncMode());",
                "m_ui.Skin = DefaultUiSkin;",
                "m_ui.AnimationsEnabled = true;",
                "m_ui.OverrideVisualMaxes = false;",
                "m_ui.Accents = UiAccentSettings{};",
                "m_StatisticsEffect =",
                "static_cast<int>(StatisticsEffect::CompleteRenderer);",
                "m_PerformanceCollapsedRequest = true;",
                "ImGui::CloseUvsrColorPickerPopup();"
            },
            "one factory-reset helper restores renderer, adapter-derived "
            "Adaptive Sync, Interface defaults, and the compact Performance "
            "endpoint before dismissing the picker");
        for (const std::string_view preservedSessionState : {
                std::string_view("m_ui = UIData{}"),
                std::string_view("m_ui.ActiveGpuAdapterIndex ="),
                std::string_view("m_ui.Camera ="),
                std::string_view("m_ui.SelectedMaterial ="),
                std::string_view("m_ui.SelectedNode ="),
                std::string_view("m_ui.ShowUI ="),
                std::string_view("m_SettingsCollapsedRequest ="),
                std::string_view("m_CommandOpen ="),
                std::string_view("m_CommandHistory.clear()") })
        {
            RequireAbsent(
                factoryReset,
                preservedSessionState,
                "factory reset preserved scene, camera, adapter, Settings "
                "shell, CLI, history, and material selection state");
        }
        Require(
            CountOccurrences(
                viewer,
                "ResetAllSettingsToFactoryDefaults();") == 2u,
            "the footer Reset and reset-settings command must be the only two "
            "callers of the shared factory-reset helper.");
        const std::string_view commandActions = ExtractSection(
            viewer,
            "bool DispatchCommandAction(",
            "void AppendDynamicCommandValues(",
            "Settings command actions");
        RequireContains(
            commandActions,
            "if (action == \"reset-settings\")\n"
            "        {\n"
            "            ResetAllSettingsToFactoryDefaults();",
            "reset-settings command shared factory-reset routing");
        RequireContains(
            viewer,
            "if (DrawCenteredActionButton(\"Reset\", actionButtonWidth))\n"
            "            ResetAllSettingsToFactoryDefaults();",
            "footer Reset shared factory-reset routing");
        RequireContains(
            catalogSource,
            "Action(\"reset-settings\", Section::Footer, "
            "\"restore renderer and interface factory settings\")",
            "reset-settings renderer-and-Interface catalog description");

        const std::string_view enumCommandHelper = ExtractSection(
            viewer,
            "static bool ApplyCommandEnum(",
            "bool DispatchUiCommandValue(",
            "enum command helper");
        const std::string compactEnumCommandHelper =
            Compact(enumCommandHelper);
        RequireContains(
            compactEnumCommandHelper,
            "boolallowSameValueMutation=false",
            "recipe-aware enum command mutation contract");
        RequireContains(
            compactEnumCommandHelper,
            "candidate==current&&!allowSameValueMutation",
            "same-tier recipe command mutation contract");
        RequireContains(
            viewer,
            "listing += \"] / \";",
            "slash-separated command list rows");
        RequireAbsent(
            viewer,
            "listing += \"] - \";",
            "retired command list row separator");

        const std::string_view uiDispatcher = ExtractSection(
            viewer,
            "bool DispatchUiCommandValue(",
            "bool DispatchGeneralCommandValue(",
            "UI command dispatcher");
        const std::string_view uiSkinDispatcher = ExtractSection(
            uiDispatcher,
            "if (path == \"ui.skin\")",
            "if (path == \"ui.visible\")",
            "UI skin command dispatcher");
        RequireOrdered(
            uiSkinDispatcher,
            {
                "std::pair<std::string_view, UiSkin>, 3",
                "{ \"amp\", UiSkin::Amp }",
                "{ \"og\", UiSkin::Og }",
                "{ \"ogg\", UiSkin::Og }",
                "DefaultUiSkin",
                "if (path == \"ui.animations\")",
                "ApplyCommandBool(",
                "m_ui.AnimationsEnabled,",
                "true,",
                "if (path == \"ui.override-visual-maxes\")",
                "ApplyCommandBool(",
                "m_ui.OverrideVisualMaxes,",
                "false,"
            },
            "canonical skin commands followed by the default-enabled "
            "animation preference and default-disabled visual-maximum "
            "override");
        RequireContains(
            catalogSource,
            "Value(\"ui.skin\", Kind::Enum, Section::Ui, "
            "\"amp|ogg\")",
            "visible Amp/Ogg command domain");
        RequireContains(
            catalogSource,
            "Value(\"ui.animations\", Kind::Boolean, Section::Ui, \"on|off\")",
            "discoverable Interface animation command");
        RequireContains(
            catalogSource,
            "Value(\"ui.override-visual-maxes\", Kind::Boolean, Section::Ui, "
            "\"on|off\")",
            "discoverable slider visual-maximum override command");
        const std::string_view uiAccentDispatcher = ExtractSection(
            uiDispatcher,
            "if (path == \"ui.accent.main\")",
            "if (path == \"ui.visible\")",
            "UI accent command dispatcher");
        RequireOrdered(
            uiAccentDispatcher,
            {
                "if (path == \"ui.accent.main\")",
                "FindUiSkinPalette(m_ui.Accents, m_ui.Skin)",
                "FindDefaultUiSkinPalette(m_ui.Skin)",
                "if (!palette || !defaultPalette)",
                "ui.accent.main is unavailable while Ogg owns stock",
                "ApplyCommandUiColorRgb(",
                "palette->primaryAccent",
                "defaultPalette->primaryAccent",
                "if (path == \"ui.accent.negative\")",
                "m_ui.Accents.secondaryAccent",
                "DefaultUiSecondaryAccent",
                "if (path == \"ui.accent.positive\")",
                "m_ui.Accents.tertiaryAccent",
                "DefaultUiTertiaryAccent",
                "if (path == \"ui.accent.secondary\")",
                "ApplyCommandUiColorRgba(",
                "if (path == \"ui.accent.tertiary\")",
                "if (path == \"ui.accent.primary\" ||",
                "path == \"ui.accent.font\" ||",
                "path == \"ui.accent.primary-background\")",
                "current = &palette->primaryAccent;",
                "current = &palette->fontColor;",
                "current = &palette->primaryBackground;",
                "*current,",
                "*defaultValue,"
            },
            "legacy RGB aliases and canonical RGBA palette command routing");
        for (const std::string_view accentPath : {
                std::string_view("ui.accent.main"),
                std::string_view("ui.accent.negative"),
                std::string_view("ui.accent.positive") })
        {
            RequireContains(
                catalogSource,
                "Value(\"" + std::string(accentPath) +
                    "\", Kind::Float3, Section::Ui, "
                    "\"display rgb float3 0..1\")",
                "Interface accent command catalog entry");
        }
        for (const std::string_view accentPath : {
                std::string_view("ui.accent.primary"),
                std::string_view("ui.accent.secondary"),
                std::string_view("ui.accent.tertiary"),
                std::string_view("ui.accent.font"),
                std::string_view("ui.accent.primary-background") })
        {
            RequireContains(
                catalogSource,
                "Value(\"" + std::string(accentPath) +
                    "\", Kind::Float4, Section::Ui, "
                    "\"display rgba float4 0..1\")",
                "canonical RGBA Interface command catalog entry");
        }
        RequireContains(
            Compact(catalogSource),
            "Action,Float4",
            "Float4 command kind appended without renumbering stable kinds");

        const std::string_view colorCommandHelpers = ExtractSection(
            viewer,
            "static std::string FormatCommandUiColorRgb(",
            "static const UiSettingsCommandDefinition*",
            "RGB and RGBA command helpers");
        RequireOrdered(
            colorCommandHelpers,
            {
                "FormatCommandUiColorRgb(color) + \" \" +",
                "FormatCommandFloat(color.alpha)",
                "static bool ApplyCommandUiColorRgb(",
                "UiRgbaColor candidate = current;",
                "arguments.size() != 3u",
                "candidate.blue",
                "current = candidate;",
                "FormatCommandUiColorRgb(current)",
                "static bool ApplyCommandUiColorRgba(",
                "UiRgbaColor candidate = current;",
                "arguments.size() != 4u",
                "arguments[3], candidate.alpha",
                "candidate.alpha < 0.f || candidate.alpha > 1.f",
                "current = candidate;",
                "FormatCommandUiColorRgba(current)"
            },
            "legacy RGB preserves stored alpha while canonical RGBA parses, "
            "validates, stores, and formats alpha");

        const std::string_view aliasingDispatcher = ExtractSection(
            viewer,
            "bool DispatchAliasingCommandValue(",
            "bool DispatchDebugCommandValue(",
            "Aliasing command dispatcher");
        const std::string_view jitterDispatcher = ExtractSection(
            aliasingDispatcher,
            "else if (path == \"anti-aliasing.taa.jitter-sequence\")",
            "else if (path == \"anti-aliasing.taa.previous-depth\")",
            "TAA Jitter Sequence command dispatcher");
        RequireOrdered(
            jitterDispatcher,
            {
                "{ \"rotated-grid-4\", Sequence::RotatedGrid4 }",
                "{ \"uniform-helix-4\", Sequence::UniformHelix4 }",
                "{ \"halton-8\", Sequence::Halton23x8 }",
                "{ \"halton-16\", Sequence::Halton23x16 }",
                "{ \"halton-32\", Sequence::Halton23x32 }",
                "{ \"sobol-32\", Sequence::Sobol32 }"
            },
            "TAA Jitter Sequence command tokens and enum mapping");
        Require(
            CountOccurrences(jitterDispatcher, "{ \"") == 6u,
            "the TAA Jitter Sequence dispatcher must expose exactly six values");
        for (const std::string_view customState : {
                std::string_view("temporalQualityCustom"),
                std::string_view("temporalCostCustom"),
                std::string_view("fastApproximateQualityCustom"),
                std::string_view("cmaa2QualityCustom"),
                std::string_view("multisampleQualityCustom") })
        {
            Require(
                CountOccurrences(aliasingDispatcher, customState) == 2u,
                "Aliasing recipe commands must permit same-tier reapplication only for " +
                    std::string(customState) + ".");
        }

        const std::string_view catalog = ExtractSection(
            catalogSource,
            "inline constexpr auto UiSettingsCommandCatalog = std::array{",
            "inline constexpr std::array<std::string_view, 5>",
            "Settings command catalog");
        const std::vector<CatalogEntry> entries = ParseCatalog(catalog);
        Require(entries.size() == 194u,
            "Settings command catalog must contain exactly 194 entries.");

        std::set<std::string> names;
        std::set<std::string> actions;
        size_t valueCount = 0u;
        for (const CatalogEntry& entry : entries)
        {
            Require(names.insert(entry.name).second,
                "Settings command names must be unique: " + entry.name);
            if (entry.action)
                actions.insert(entry.name);
            else
                ++valueCount;
        }
        Require(valueCount == 190u,
            "Settings command catalog must contain exactly 190 values.");
        Require(actions == std::set<std::string>{
                "open-scene-folder",
                "reset-settings",
                "restart",
                "capture"
            },
            "Settings command catalog must retain exactly four actions.");

        for (const std::string_view path : {
                std::string_view("gpu.adaptive-sync"),
                std::string_view("noise.pattern"),
                std::string_view("noise.resolution"),
                std::string_view("noise.animate-samples"),
                std::string_view("visibility.specify-noise"),
                std::string_view("visibility.noise-pattern"),
                std::string_view("visibility.noise-resolution"),
                std::string_view("visibility.animate-samples"),
                std::string_view("anti-aliasing.taa.enabled"),
                std::string_view("anti-aliasing.taa.jitter-sequence"),
                std::string_view("anti-aliasing.taa.previous-depth"),
                std::string_view("anti-aliasing.fxaa.enabled"),
                std::string_view("anti-aliasing.fxaa.quality"),
                std::string_view(
                    "anti-aliasing.fxaa.minimum-edge-threshold"),
                std::string_view("anti-aliasing.cmaa2.enabled"),
                std::string_view("anti-aliasing.cmaa2.edge-threshold"),
                std::string_view("anti-aliasing.cmaa2.detector"),
                std::string_view("anti-aliasing.msaa.enabled"),
                std::string_view("anti-aliasing.msaa.quality"),
                std::string_view("debug.world.materials"),
                std::string_view("debug.visibility.view"),
                std::string_view("debug.pbr.filter"),
                std::string_view(
                    "representation.bvh.build-preference"),
                std::string_view("representation.blas.update-mode"),
                std::string_view("representation.tlas.update-mode"),
                std::string_view("representation.allow-ray-traversal"),
                std::string_view("visibility.ao.output-hit-distance"),
                std::string_view("visibility.gi.output-hit-distance"),
                std::string_view("denoising.ao.method"),
                std::string_view("denoising.gi.method"),
                std::string_view("denoising.shadows.method"),
                std::string_view("denoising.sky.method"),
                std::string_view("sky.visibility.enabled"),
                std::string_view("sky.visibility.diffuse-ibl"),
                std::string_view("sky.visibility.specular-ibl"),
                std::string_view("sky.visibility.ratio-estimator"),
                std::string_view("sky.visibility.output-hit-distance"),
                std::string_view("sky.visibility.samples-per-pixel"),
                std::string_view("sky.visibility.specify-noise"),
                std::string_view("sky.visibility.noise-pattern"),
                std::string_view("sky.visibility.noise-resolution"),
                std::string_view("sky.visibility.animate-samples"),
                std::string_view("sky.visibility.max-distance"),
                std::string_view("sky.visibility.ray-bias"),
                std::string_view("sky.auto-exposure.enabled"),
                std::string_view(
                    "sky.auto-exposure.exposure-compensation"),
                std::string_view(
                    "sky.auto-exposure.maximum-brightening"),
                std::string_view(
                    "sky.auto-exposure.maximum-darkening"),
                std::string_view("sky.auto-exposure.adjustment-period"),
                std::string_view("shadows.ray-traced.enabled"),
                std::string_view(
                    "shadows.ray-traced.ratio-estimator"),
                std::string_view(
                    "shadows.ray-traced.output-hit-distance"),
                std::string_view(
                    "shadows.ray-traced.samples-per-pixel"),
                std::string_view(
                    "shadows.ray-traced.specify-noise"),
                std::string_view(
                    "shadows.ray-traced.hard-shadows"),
                std::string_view(
                    "shadows.ray-traced.noise-pattern"),
                std::string_view(
                    "shadows.ray-traced.noise-resolution"),
                std::string_view(
                    "shadows.ray-traced.animate-samples"),
                std::string_view(
                    "shadows.ray-traced.max-distance"),
                std::string_view("shadows.ray-traced.ray-bias"),
                std::string_view(
                    "light.selected.flashlight.output-hit-distance"),
                std::string_view(
                    "light.selected.flashlight.angular-size"),
                std::string_view(
                    "light.selected.flashlight.horizontal-offset"),
                std::string_view(
                    "light.selected.flashlight.vertical-offset") })
        {
            Require(names.find(std::string(path)) != names.end(),
                "Settings command catalog is missing " + std::string(path));
        }
        for (const std::string_view retiredPath : {
                std::string_view("anti-aliasing.method"),
                std::string_view("visibility-benchmark"),
                std::string_view("sample-resurrection"),
                std::string_view("shadows.svsm"),
                std::string_view("shadows.csm"),
                std::string_view("debug.shadows.edge-overlay"),
                std::string_view("debug.shadows.overlay-opacity"),
                std::string_view("debug.shadows.isolation"),
                std::string_view("shadows.directional.technique"),
                std::string_view("shadows.ratio-estimator"),
                std::string_view("shadows.screen-space-directional"),
                std::string_view("debug.visibility.indirect-diffuse-only"),
                std::string_view("visibility.ao.power"),
                std::string_view("sky.auto-exposure.brightness"),
                std::string_view("buffers."),
                std::string_view("statistics.") })
        {
            for (const std::string& name : names)
            {
                Require(name.find(retiredPath) == std::string::npos,
                    "retired Settings command path returned: " + name);
            }
        }

        const std::string_view representationDispatcher = ExtractSection(
            viewer,
            "bool DispatchRepresentationCommandValue(",
            "bool DispatchNoiseCommandValue(",
            "Representation command dispatcher");
        const std::string_view noiseDispatcher = ExtractSection(
            viewer,
            "bool DispatchNoiseCommandValue(",
            "bool DispatchVisibilityCommandValue(",
            "Noise command dispatcher");
        const std::string_view directionalShadowDispatcher = ExtractSection(
            viewer,
            "bool DispatchDirectionalShadowCommandValue(",
            "bool DispatchMaterialCommandValue(",
            "Directional Shadow command dispatcher");
        const std::string_view denoisingDispatcher = ExtractSection(
            viewer,
            "bool DispatchDenoisingCommandValue(",
            "bool DispatchFlashlightCommandValue(",
            "Denoising command dispatcher");

        const std::map<std::string, std::string_view> dispatchers = {
            {
                "Ui",
                ExtractSection(viewer,
                    "bool DispatchUiCommandValue(",
                    "bool DispatchGeneralCommandValue(",
                    "UI command dispatcher")
            },
            {
                "General",
                ExtractSection(viewer,
                    "bool DispatchGeneralCommandValue(",
                    "bool DispatchRepresentationCommandValue(",
                    "General command dispatcher")
            },
            {
                "Representation",
                representationDispatcher
            },
            {
                "Noise",
                noiseDispatcher
            },
            {
                "Visibility",
                ExtractSection(viewer,
                    "bool DispatchVisibilityCommandValue(",
                    "bool DispatchAliasingCommandValue(",
                    "Visibility command dispatcher")
            },
            {
                "Aliasing",
                aliasingDispatcher
            },
            {
                "Debug",
                ExtractSection(viewer,
                    "bool DispatchDebugCommandValue(",
                    "bool DispatchSkyCommandValue(",
                    "Debug command dispatcher")
            },
            {
                "Sky",
                ExtractSection(viewer,
                    "bool DispatchSkyCommandValue(",
                    "bool DispatchDenoisingCommandValue(",
                    "Sky command dispatcher")
            },
            {
                "Denoising",
                denoisingDispatcher
            },
            {
                "Lights",
                ExtractSection(viewer,
                    "struct FlashlightFloatCommandBinding",
                    "bool DispatchDirectionalShadowCommandValue(",
                    "Lights command dispatcher")
            },
            {
                "DirectionalShadows",
                directionalShadowDispatcher
            },
            {
                "Materials",
                ExtractSection(viewer,
                    "bool DispatchMaterialCommandValue(",
                    "bool DispatchCommandValue(",
                    "Material command dispatcher")
            }
        };
        for (const CatalogEntry& entry : entries)
        {
            if (entry.action)
                continue;
            const auto dispatcher = dispatchers.find(entry.section);
            Require(dispatcher != dispatchers.end(),
                "No value dispatcher exists for section " + entry.section);
            if (dispatcher != dispatchers.end())
            {
                if (entry.section == "Denoising")
                {
                    const size_t propertySeparator = entry.name.rfind('.');
                    const std::string prefix = entry.name.substr(
                        0u, propertySeparator + 1u);
                    const std::string property = entry.name.substr(
                        propertySeparator + 1u);
                    Require(
                        ContainsQuotedLiteral(dispatcher->second, prefix) &&
                            ContainsQuotedLiteral(
                                dispatcher->second, property),
                        "The Denoising dispatcher is missing " + entry.name);
                }
                else
                {
                    Require(
                        ContainsQuotedLiteral(dispatcher->second, entry.name),
                        "The " + entry.section + " dispatcher is missing " +
                            entry.name);
                }
            }
        }

        const std::string_view lightsDispatcher =
            dispatchers.at("Lights");
        RequireContains(
            lightsDispatcher,
            "FlashlightFloatCommandBinding, 12>",
            "flashlight Float command binding count");
        RequireAbsent(
            lightsDispatcher,
            "light.selected.flashlight.adjustment-speed",
            "retired flashlight Adjustment Speed command");
        RequireAbsent(
            lightsDispatcher,
            "light.selected.flashlight.time-to-action",
            "retired flashlight Time to Action command");

        const std::string compactRepresentationDispatcher =
            Compact(representationDispatcher);
        for (const std::string_view mapping : {
                std::string_view(
                    "{\"fast-trace\",BvhBuildPreference::FastTrace}"),
                std::string_view(
                    "{\"balanced\",BvhBuildPreference::Balanced}"),
                std::string_view(
                    "{\"fast-build\",BvhBuildPreference::FastBuild}"),
                std::string_view("{\"rebuild\",BlasUpdateMode::Rebuild}"),
                std::string_view("{\"refit\",BlasUpdateMode::Refit}"),
                std::string_view("{\"rebuild\",TlasUpdateMode::Rebuild}"),
                std::string_view("{\"refit\",TlasUpdateMode::Refit}") })
        {
            RequireContains(
                compactRepresentationDispatcher,
                mapping,
                "Representation command token and enum mapping");
        }
        RequireContains(
            compactRepresentationDispatcher,
            "GetWorldSpaceRepresentationInvalidation("
            "m_ui.Representation,candidate)",
            "Representation command invalidation classification");
        RequireContains(
            compactRepresentationDispatcher,
            "m_app->InvalidateWorldSpaceRepresentation(invalidation)",
            "Representation command invalidation request");

        const std::string compactNoiseDispatcher = Compact(noiseDispatcher);
        for (const std::string_view mapping : {
                std::string_view(
                    "{\"spatial-white\",NoisePattern::SpatialWhite}"),
                std::string_view(
                    "{\"spatial-blue\",NoisePattern::SpatialBlue}"),
                std::string_view(
                    "{\"spatiotemporal-blue\","
                    "NoisePattern::SpatiotemporalBlue}"),
                std::string_view(
                    "{\"64x64\",NoiseResolution::Size64}"),
                std::string_view(
                    "{\"128x128\",NoiseResolution::Size128}"),
                std::string_view(
                    "{\"256x256\",NoiseResolution::Size256}"),
                std::string_view(
                    "{\"512x512\",NoiseResolution::Size512}") })
        {
            RequireContains(
                compactNoiseDispatcher,
                mapping,
                "Noise command token and enum mapping");
        }
        RequireContains(
            compactNoiseDispatcher,
            "m_app->ResetNoiseSamplingHistory("
            "!m_ui.ScreenSpaceVisibility.noise.specifyNoise,"
            "!m_ui.DirectionalShadows.ratioEstimator.noise.specifyNoise,"
            "!m_ui.RayTracedSkyVisibility.noise.specifyNoise,true)",
            "global Noise history isolation");

        const std::string compactDirectionalShadowDispatcher =
            Compact(directionalShadowDispatcher);
        for (const std::string_view mapping : {
                std::string_view("{\"1\",0}"),
                std::string_view("{\"2\",1}"),
                std::string_view("{\"4\",2}"),
                std::string_view("{\"8\",3}"),
                std::string_view("{\"16\",4}"),
                std::string_view("{\"32\",5}"),
                std::string_view("{\"64\",6}") })
        {
            RequireContains(
                compactDirectionalShadowDispatcher,
                mapping,
                "Directional Shadow command token and enum mapping");
        }
        for (const std::string_view retired : {
                std::string_view("{\"1/16\",-4}"),
                std::string_view("{\"1/8\",-3}"),
                std::string_view("{\"1/4\",-2}"),
                std::string_view("{\"1/2\",-1}"),
                std::string_view("hashed-white-noise") })
        {
            RequireAbsent(
                compactDirectionalShadowDispatcher,
                retired,
                "retired Directional Shadow command value");
        }
        RequireContains(
            compactDirectionalShadowDispatcher,
            "IsHeitzRatioEstimatorConfigurationSupported("
            "candidate.ratioEstimator)",
            "ratio-estimator command validation");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "candidate.ratioEstimator.rayBias,"
            "factoryDefaults.ratioEstimator.rayBias,0.f,"
            "HeitzRatioEstimatorMaximumRayBias",
            "ratio-estimator ray-bias command bounds");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "candidate.ratioEstimator.hardShadows,"
            "factoryDefaults.ratioEstimator.hardShadows",
            "ratio-estimator hard-shadow command");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "candidate.ratioEstimator.noise.custom.pattern,"
            "factoryDefaults.ratioEstimator.noise.custom.pattern",
            "ratio-estimator noise-pattern command");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "candidate.ratioEstimator.noise.custom.animate,"
            "factoryDefaults.ratioEstimator.noise.custom.animate",
            "ratio-estimator sample-animation command");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "candidate.ratioEstimator.noise.custom.resolution,"
            "factoryDefaults.ratioEstimator.noise.custom.resolution",
            "ratio-estimator noise-resolution command");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "candidate.ratioEstimator.noise.specifyNoise,"
            "factoryDefaults.ratioEstimator.noise.specifyNoise",
            "ratio-estimator Specify Noise command");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "candidate.ratioEstimator.maxDistance,"
            "factoryDefaults.ratioEstimator.maxDistance",
            "ratio-estimator max-distance command");
        for (const std::string_view mapping : {
                std::string_view(
                    "{\"max\",RayVisibilityMaxDistance::Maximum}"),
                std::string_view(
                    "{\"32m\",RayVisibilityMaxDistance::Meters32}"),
                std::string_view(
                    "{\"2m\",RayVisibilityMaxDistance::Meters2}") })
        {
            RequireContains(
                compactDirectionalShadowDispatcher,
                mapping,
                "ratio-estimator max-distance token and enum mapping");
        }
        RequireContains(
            compactDirectionalShadowDispatcher,
            "candidate.ratioEstimator.enabled,"
            "factoryDefaults.ratioEstimator.enabled",
            "ray traced shadow enable command");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "candidate.ratioEstimator.useRatioEstimator,"
            "factoryDefaults.ratioEstimator.useRatioEstimator",
            "independent ratio estimator command");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "candidate.ratioEstimator.outputHitDistance,"
            "factoryDefaults.ratioEstimator.outputHitDistance",
            "optional sun hit distance command");
        RequireAbsent(
            compactDirectionalShadowDispatcher,
            "ScreenSpaceDirectionalShadows",
            "ratio-estimator commands mutating screen-space state");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "!m_app->HasPrimaryDirectionalLight()",
            "directional-shadow command light validation");
        RequireContains(
            compactDirectionalShadowDispatcher,
            "!m_app->SupportsHeitzRatioEstimatorShadows()",
            "ratio-estimator command device validation");

        const std::string compactDenoisingDispatcher =
            Compact(denoisingDispatcher);
        for (const std::string_view signal : {
                std::string_view("denoising.ao."),
                std::string_view("denoising.gi."),
                std::string_view("denoising.shadows."),
                std::string_view("denoising.sky.") })
        {
            RequireContains(
                compactDenoisingDispatcher,
                signal,
                "Denoising signal command routing");
        }
        RequireContains(
            compactDenoisingDispatcher,
            "candidate=SanitizeDenoisingSettings(effect,candidate)",
            "Denoising signal sanitization");
        for (const std::string_view mapping : {
                std::string_view(
                    "{\"reblur\",DenoisingMethodChoice::Reblur}"),
                std::string_view(
                    "{\"relax\",DenoisingMethodChoice::Relax}"),
                std::string_view(
                    "{\"sigma\",DenoisingMethodChoice::Sigma}") })
        {
            RequireContains(
                compactDenoisingDispatcher,
                mapping,
                "Denoising method command mapping");
        }

        const std::string_view router = ExtractSection(
            viewer,
            "bool DispatchCommandValue(",
            "bool DispatchCommandAction(",
            "Settings command section router");
        for (const std::string_view section : {
                std::string_view("Ui"),
                std::string_view("General"),
                std::string_view("Representation"),
                std::string_view("Noise"),
                std::string_view("Visibility"),
                std::string_view("Denoising"),
                std::string_view("Aliasing"),
                std::string_view("Debug"),
                std::string_view("Sky"),
                std::string_view("Lights"),
                std::string_view("DirectionalShadows"),
                std::string_view("Materials") })
        {
            RequireContains(
                router,
                "case UiSettingsCommandSection::" + std::string(section) +
                    ":",
                "Settings command section router");
        }
        RequireContains(
            Compact(viewer),
            "UiSettingsCommandBindings.size()=="
            "UiSettingsCommandCatalog.size()",
            "catalog-derived command binding parity");
        RequireAbsent(
            catalogSource,
            "FactoryMutationPolicy",
            "retired factory command taxonomy");
    }

    void ValidateMaterialHistoryInvalidation(std::string_view viewer)
    {
        const std::string_view frontEllipsis = ExtractSection(
            viewer,
            "struct FrontEllipsisText",
            "struct StatSnapshot",
            "Unicode-safe front ellipsis formatter");
        RequireOrdered(
            frontEllipsis,
            {
                "std::string display;",
                "bool truncated = false;",
                "FormatFrontEllipsisUtf8(",
                "size_t maximumCodePoints)",
                "while (cursor < end && codePointCount < maximumCodePoints)",
                "ImTextCharFromUtf8(",
                "cursor += byteCount > 0 ? byteCount : 1;",
                "result.truncated = cursor < end;",
                "result.display.assign(begin, cursor);",
                "if (result.truncated)",
                "result.display += \"...\";"
            },
            "front ellipsis counts complete UTF-8 code points and appends "
            "three literal dots only when a 26th character exists");

        const std::string_view notification = ExtractSection(
            viewer,
            "void NotifyMaterialCommandChanged(",
            "void SynchronizeAntiAliasingSettings(",
            "material mutation notification");
        RequireOrdered(
            notification,
            {
                "material->dirty = true;",
                "InvalidateContent();",
                "ResetImageBasedLightingHistory();"
            },
            "material mutation notification");

        const std::string_view materialWindow = ExtractSection(
            viewer,
            "void DrawMaterialDrawer(float settingsControlWidth)",
            "static std::string BuildPerformanceLine(",
            "Material editor mutation routing");
        for (const std::string_view drawerContract : {
                std::string_view("\"Material\""),
                std::string_view(
                    "BeginDrawerBody(\"##MaterialBody\", settingsControlWidth);"),
                std::string_view("m_MaterialDrawerAppearance ="),
                std::string_view("Refresh Center Material") })
        {
            RequireContains(
                materialWindow,
                drawerContract,
                "embedded Material drawer contract");
        }
        RequireContains(
            materialWindow,
            "[app = m_app, material, candidate]()",
            "Material Domain deferred mutation routing");
        RequireContains(
            materialWindow,
            "app->NotifyMaterialCommandChanged(material);",
            "Material Domain history invalidation");
        RequireOrdered(
            materialWindow,
            {
                "const bool materialChanged =",
                "donut::app::MaterialEditor(",
                "if (materialChanged)",
                "m_app->NotifyMaterialCommandChanged(material);"
            },
            "Material editor history invalidation");
        RequireAbsent(
            materialWindow,
            "##MaterialControlsBody",
            "retired duplicate translucent Material editor surface");
        const std::string_view materialDrawerBody = ExtractSection(
            materialWindow,
            "void DrawMaterialDrawer(float settingsControlWidth)",
            "void DrawInterfaceDrawer(float settingsControlWidth)",
            "Material drawer body");
        Require(
            CountOccurrences(materialDrawerBody, "BeginDrawerBody(") == 1u &&
                CountOccurrences(materialDrawerBody, "EndDrawerBody();") == 1u,
            "Material uses only its ordinary drawer body around the editor.");
        RequireOrdered(
            materialDrawerBody,
            {
                "const FrontEllipsisText formattedMaterialName =",
                "FormatFrontEllipsisUtf8(material->name, 25u);",
                "const std::string materialPrefix =",
                "\"Material \" + std::to_string(material->materialID) +",
                "ImGui::BeginGroup();",
                "ImGui::TextUnformatted(materialPrefix.c_str());",
                "ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);",
                "ImGui::TextColored(",
                "g_UiVisualTokens.successText,",
                "formattedMaterialName.display.c_str());",
                "ImGui::EndGroup();",
                "if (formattedMaterialName.truncated)",
                "FormatFrontEllipsisUtf8(material->name, 117u);",
                "ImGui::SetItemTooltip(\"%s\", tooltip.display.c_str());",
                "ImGui::PushID(material->materialID);",
                "const donut::app::MaterialEditorCallbacks materialCallbacks = {",
                "&BeginMaterialEditorConditionalRegion,",
                "&EndMaterialEditorConditionalRegion,",
                "&DrawMaterialEditorTextureFilename,",
                "&DrawMaterialEditorColorEdit3",
                "&materialCallbacks);",
                "ImGui::PopID();"
            },
            "Material names use a deterministic Unicode-safe 25-character "
            "front cutoff, a default-color ID prefix, success-color name suffix, "
            "truncated-only full-name tooltip, per-material ID scope, and one "
            "callback bridge for conditional rows, filenames, and color edits");
        RequireAbsent(
            materialDrawerBody,
            "fullMaterialLabel",
            "retired monolithic Material label that colored the prefix");
        RequireAbsent(
            materialDrawerBody,
            "ImGui::RenderTextEllipsis(",
            "retired width-dependent Material label ellipsis");

        const std::string_view drawerVisibility = ExtractSection(
            viewer,
            "void SetMaterialDrawerVisible(",
            "const Material* GetOriginalMaterial(",
            "Material drawer visibility contract");
        RequireOrdered(
            drawerVisibility,
            {
                "m_ui.ShowUI = true;",
                "m_ui.ShowMaterialDrawer = true;",
                "MaterialPickPurpose::RefreshMaterialDrawerSelection"
            },
            "open Material drawer and request center selection");
        const std::string_view materialReveal = ExtractSection(
            viewer,
            "void RequestMaterialDrawerVisible(",
            "const GpuAdapterChoice* GetActiveGpuAdapterChoice() const",
            "Material drawer reveal contract");
        RequireOrdered(
            materialReveal,
            {
                "m_app->SetMaterialDrawerVisible(visible, refreshSelection);",
                "m_MaterialRevealRequested = visible;",
                "m_SettingsCollapsedRequest = false;",
                "m_SettingsCollapsed = false;"
            },
            "Material opening must expand Settings and request reveal");
        RequireOrdered(
            materialWindow,
            {
                "m_MaterialRevealRequested && targetOpen",
                "ImGui::SetScrollHereY(0.f);",
                "m_MaterialRevealRequested = false;"
            },
            "Material opening must scroll the embedded drawer into view");
        RequireContains(
            viewer,
            "bool m_MaterialDrawerPresentationForceClosed = false;",
            "one-shot Material presentation reset state");
        RequireOrdered(
            materialWindow,
            {
                "DrawCollapsingHeader(",
                "ImGuiTreeNodeFlags_None,",
                "m_MaterialDrawerPresentationForceClosed);",
                "m_MaterialDrawerPresentationForceClosed = false;",
                "m_MaterialDrawerAppearance ="
            },
            "Material consumes its forced-closed presentation before reading "
            "the new drawer state");
        const std::string_view collapsedMaterialReset = ExtractSection(
            viewer,
            "const bool settingsCollapsed =",
            "UiBackdropRect& performanceTitleBackdrop =",
            "collapsed Settings Material reset");
        RequireOrdered(
            collapsedMaterialReset,
            {
                "if (settingsCollapsed)",
                "const bool materialPresentationWasActive =",
                "m_ui.ShowMaterialDrawer ||",
                "m_MaterialDrawerAppearance > 0.f;",
                "if (m_ui.ShowMaterialDrawer)",
                "RequestMaterialDrawerVisible(false, false);",
                "m_MaterialDrawerAppearance = 0.f;",
                "m_MaterialDrawerPresentationForceClosed =",
                "m_MaterialDrawerPresentationForceClosed ||",
                "materialPresentationWasActive;"
            },
            "collapsing Settings immediately closes Material, clears its "
            "crosshair appearance, and arms a closed next submission");
        RequireContains(
            viewer,
            "SmoothPixelZoomVisibility(m_MaterialDrawerAppearance)",
            "Material drawer crosshair visibility");
        RequireOrdered(
            viewer,
            {
                "const bool shadowsOpen = DrawCollapsingHeader(",
                "DrawMaterialDrawer(settingsControlWidth);",
                "DrawInterfaceDrawer(settingsControlWidth);",
                "TrackSettingsScrollAnchor(\n"
                "            ImGui::GetID(\"##SettingsFooterAnchor\")"
            },
            "Material immediately precedes the final Interface drawer and "
            "Settings footer actions");
        RequireContains(
            viewer,
            "ResolvePerformanceMaximumWindowHeight(",
            "CLI-aware detached Performance window height cap");

        const std::string_view materialDispatcher = ExtractSection(
            viewer,
            "bool DispatchMaterialCommandValue(",
            "bool DispatchCommandValue(",
            "Material command mutation routing");
        RequireOrdered(
            materialDispatcher,
            {
                "*material = candidate;",
                "m_app->NotifyMaterialCommandChanged(material);"
            },
            "Material command history invalidation");
    }

    void ValidateUiSafety(
        std::string_view viewer,
        std::string_view uiAnimation,
        std::string_view donutAppOverride,
        std::string_view donutAppUiPolishOverride,
        std::string_view imguiUiOverride,
        std::string_view imguiSliderOverride,
        std::string_view imguiComboRollOverride,
        std::string_view imguiUiPolishOverride,
        std::string_view imguiTooltipPickerOverride,
        std::string_view imguiUpstream,
        std::string_view backdropBlurShader,
        std::string_view cmakeSource)
    {
        const std::string donutAppUiPolishAdded =
            AddedPatchLines(donutAppUiPolishOverride);
        const std::string imguiTooltipPickerAdded =
            AddedPatchLines(imguiTooltipPickerOverride);
        const std::string_view visualTokens = ExtractSection(
            viewer,
            "struct UiVisualTokens",
            "struct StatSnapshot",
            "UI visual tokens");
        RequireContains(
            visualTokens,
            "ImVec4 errorText;",
            "dynamic semantic failure accent token");
        RequireContains(
            visualTokens,
            "ImVec4 successText;",
            "dynamic semantic success accent token");
        RequireContains(
            visualTokens,
            "ImVec4 panelInsetFrame;",
            "shared opaque root-panel inset frame token");
        RequireContains(
            visualTokens,
            "ImVec4 colorPickerSurface;",
            "skin-resolved scoped color-picker surface token");
        RequireContains(
            visualTokens,
            "float controlDisabledAlpha = 0.60f;",
            "skin-resolved authored control disabled opacity token");
        RequireContains(
            viewer,
            "tokens.controlDisabledAlpha = style.DisabledAlpha;",
            "active skin captures its disabled opacity before neutral root locks");
        const std::string_view authoredCornerRounding = ExtractSection(
            viewer,
            "constexpr float AuthoredCornerRounding = 4.f;",
            "const UiSkinBehavior behavior =",
            "unified authored corner rounding");
        RequireOrdered(
            authoredCornerRounding,
            {
                "constexpr float AuthoredCornerRounding = 4.f;",
                "style.WindowRounding = AuthoredCornerRounding;",
                "style.ChildRounding = AuthoredCornerRounding;",
                "style.PopupRounding = AuthoredCornerRounding;",
                "style.FrameRounding = AuthoredCornerRounding;",
                "style.GrabRounding = AuthoredCornerRounding;",
                "style.ScrollbarRounding = AuthoredCornerRounding;",
                "style.ScrollbarSize = 12.f;",
                "style.TabRounding = AuthoredCornerRounding;",
                "tokens.drawerRounding = style.ChildRounding;"
            },
            "one four-pixel authored radius for roots, drawers, popups, "
            "controls, grabs, scrollbars, and tabs with a twelve-pixel channel");
        RequireContains(
            viewer,
            "tokens.drawerRounding *= safeDisplayScale;",
            "unified drawer radius display scaling");
        RequireOrdered(
            viewer,
            {
                "tokens.errorText = MakeUiColor(accents.secondaryAccent);",
                "tokens.successText = MakeUiColor(accents.tertiaryAccent);",
                "ImGui::SetUvsrUiAccentColors(",
                "MakeUiColor(accents.secondaryAccent),",
                "MakeUiColor(accents.tertiaryAccent));"
            },
            "one alpha-preserving secondary/tertiary route for product text "
            "and semantic toggles");
        RequireContains(
            donutAppOverride,
            "+    const math::float4& positiveAccent)",
            "Material editor RGBA positive-accent parameter");
        RequireOrdered(
            donutAppUiPolishAdded,
            {
                "struct MaterialEditorCallbacks",
                "bool (*beginConditionalRegion)(const char* id, bool visible) = nullptr;",
                "void (*endConditionalRegion)() = nullptr;",
                "void (*drawTextureFilename)(const char* filename, const math::float4& color) = nullptr;",
                "bool (*drawColorEdit3)(const char* label, float* color) = nullptr;",
                "const MaterialEditorCallbacks* callbacks = nullptr);",
                "const bool useConditionalRegions =",
                "callbacks->beginConditionalRegion != nullptr &&",
                "callbacks->endConditionalRegion != nullptr;",
                "const auto drawTextureFilename =",
                "callbacks->drawTextureFilename(",
                "const auto drawColorEdit3 =",
                "callbacks->drawColorEdit3 != nullptr",
                "return callbacks->drawColorEdit3(label, color);",
                "##MaterialSpecularGlossRegion",
                "material->useSpecularGlossModel",
                "##MaterialMetalRoughRegion",
                "!material->useSpecularGlossModel",
                "if (beginConditionalRegion(\"##MaterialOpacityRegion\", alphaBlended))",
                "##MaterialAlphaCutoffRegion",
                "##MaterialNormalScaleRegion",
                "##MaterialOcclusionStrengthRegion",
                "##MaterialTransmissionRegion"
            },
            "the first-party Donut bridge supplies seven balanced conditional "
            "regions plus filename and ColorEdit3 callbacks with raw null-"
            "callback fallbacks");
        Require(
            CountOccurrences(
                donutAppUiPolishAdded,
                "drawTextureFilename(material->") == 9u,
            "Every one of the nine blue Material texture-name paths must use "
            "the shared 25-character renderer.");
        Require(
            CountOccurrences(
                donutAppUiPolishAdded,
                "\n        endConditionalRegion();") == 7u,
            "Each animated Material conditional region must close exactly once.");
        Require(
            CountOccurrences(
                donutAppUiPolishAdded,
                "update |= drawColorEdit3(") == 4u &&
                CountOccurrences(
                    donutAppUiPolishAdded,
                    "ImGuiColorEditFlags_NoTooltip") == 4u,
            "All four Material RGB routes must use the callback/fallback helper "
            "and suppress the stock color-preview tooltip.");
        RequireOrdered(
            donutAppUiPolishAdded,
            {
                "##MaterialNormalScaleRegion",
                "material->enableNormalTexture",
                "const char* normalScaleLabel = \"Normal Scale\";",
                "ImGui::CalcTextSize(normalScaleLabel).x +",
                "ImGui::GetStyle().ItemInnerSpacing.x;",
                "ImGui::GetContentRegionAvail().x - normalScaleLabelWidth;",
                "ImGui::SetNextItemWidth(",
                "normalScaleSliderWidth > 1.f ? normalScaleSliderWidth : 1.f);",
                "update |= ImGui::SliderFloat(",
                "normalScaleLabel,",
                "&material->normalTextureScale,",
                "-2.f,",
                "2.f);"
            },
            "Normal Scale reserves its visible label width inside the Material "
            "content lane and keeps the safe bounded slider in its conditional "
            "region");
        RequireAbsent(
            donutAppUiPolishAdded,
            "ImGui::Button(\"1.0\")",
            "retired Material Normal Scale reset button");
        const std::string_view materialCallbackBridge = ExtractSection(
            viewer,
            "static bool BeginMaterialEditorConditionalRegion(",
            "static float GetUiHighlightFade(",
            "UVSR Material editor callback bridge");
        RequireOrdered(
            materialCallbackBridge,
            {
                "BeginAnimatedToggleRegion(",
                "UiToggleRegionOwner::Settings);",
                "static void EndMaterialEditorConditionalRegion()",
                "EndAnimatedToggleRegion();",
                "static void DrawMaterialEditorTextureFilename(",
                "FormatFrontEllipsisUtf8(fullFilename, 25u);",
                "ImGui::TextColored(",
                "if (formatted.truncated)",
                "FormatFrontEllipsisUtf8(fullFilename, 117u);",
                "ImGui::SetItemTooltip(\"%s\", tooltip.display.c_str());",
                "enum class UvsrColorEditChannels",
                "Rgb,",
                "Rgba",
                "static bool DrawUvsrColorEdit(",
                "ImGuiColorEditFlags_Float |",
                "static bool DrawMaterialEditorColorEdit3(",
                "return DrawUvsrColorEdit(",
                "UvsrColorEditChannels::Rgb);"
            },
            "Material conditional rows reuse the reversible Settings timeline "
            "while truncated filenames expose their full basename and Material "
            "RGB edits route through the shared first-party policy");
        RequireOrdered(
            materialCallbackBridge,
            {
                "ImGuiColorEditFlags_Float |",
                "ImGuiColorEditFlags_DisplayRGB |",
                "ImGuiColorEditFlags_NoTooltip;",
                "ImGuiColorEditFlags_AlphaBar |",
                "ImGuiColorEditFlags_AlphaPreviewHalf;",
                "if (!ImGui::IsUvsrStockWidgetRenderingEnabled())",
                "flags |= ImGuiColorEditFlags_PickerHueWheel;",
                "? ImGui::ColorEdit4(label, color, flags)",
                ": ImGui::ColorEdit3(label, color, flags);"
            },
            "one shared first-party wrapper owns RGB/RGBA flags and stock versus "
            "authored picker selection");
        Require(
            CountOccurrences(viewer, "ImGui::ColorEdit3(") == 1u &&
                CountOccurrences(viewer, "ImGui::ColorEdit4(") == 1u &&
                CountOccurrences(viewer, "DrawUvsrColorEdit(") == 5u,
            "Production source may call raw ColorEdit3/4 only inside the shared "
            "wrapper, which serves Interface, Material, flashlight, and lights.");
        RequireOrdered(
            viewer,
            {
                "donut::app::MaterialEditor(",
                "g_UiVisualTokens.successText.x,",
                "g_UiVisualTokens.successText.y,",
                "g_UiVisualTokens.successText.z,",
                "g_UiVisualTokens.successText.w"
            },
            "dynamic RGBA positive accent reaches Material status text");

        const std::string_view backdropSlots = ExtractSection(
            viewer,
            "constexpr size_t UiPerformanceTitleBackdropIndex",
            "struct UiBackdropRect",
            "named UI backdrop slots");
        RequireOrdered(
            backdropSlots,
            {
                "UiPerformanceTitleBackdropIndex = 0u;",
                "UiPerformanceBodyBackdropIndex = 1u;",
                "UiSettingsTitleBackdropIndex = 2u;",
                "UiSettingsBodyBackdropIndex = 3u;",
                "UiCommandBackdropIndex = 4u;",
                "UiBackdropRectCount = 5u;",
                "UiBackdropCornersAll = 0xFu;",
                "struct UiBackdropExclusionRect"
            },
            "four independent root surfaces and fifth command backdrop slot");
        RequireContains(
            viewer,
            "std::array<UiBackdropRect, UiBackdropRectCount>",
            "backdrop storage sized by the five-slot contract");
        RequireContains(
            viewer,
            "std::vector<UiBackdropExclusionRect> compositeExclusions;",
            "header-support exclusion storage on each surface backdrop");
        const std::string_view panelSurfaceBackdropCapture = ExtractSection(
            viewer,
            "static void CapturePanelSurfaceBackdrops(",
            "static void ApplyWindowAppearance(",
            "independent root title/body backdrop capture");
        RequireOrdered(
            panelSurfaceBackdropCapture,
            {
                "titleBackdrop.cornerMask = UiBackdropCornersAll;",
                "titleBackdrop.shadowBlur = 0.f;",
                "titleBackdrop.shadowOpacity = 0.f;",
                "titleBackdrop.shadowOffsetY = 0.f;",
                "titleBackdrop.composite = true;",
                "bodyBackdrop.cornerMask = UiBackdropCornersAll;",
                "bodyBackdrop.shadowBlur = 0.f;",
                "bodyBackdrop.shadowOpacity = 0.f;",
                "bodyBackdrop.shadowOffsetY = 0.f;",
                "bodyBackdrop.composite = true;",
                "bodyBackdrop.visible = expanded"
            },
            "each title/body blur surface is fully rounded, shadowless, and "
            "independently visible");
        const std::string_view panelBackdropRouting = ExtractSection(
            viewer,
            "UiBackdropRect& performanceTitleBackdrop =",
            "ApplyWindowAppearance(\n"
            "            performanceWindowDrawList",
            "root surface backdrop and exclusion routing");
        RequireOrdered(
            panelBackdropRouting,
            {
                "UiPerformanceTitleBackdropIndex",
                "UiPerformanceBodyBackdropIndex",
                "UiSettingsTitleBackdropIndex",
                "UiSettingsBodyBackdropIndex",
                "CapturePanelSurfaceBackdrops(",
                "performanceTitleBackdrop,",
                "performanceBodyBackdrop,",
                "CapturePanelSurfaceBackdrops(",
                "settingsTitleBackdrop,",
                "settingsBodyBackdrop,",
                "if (g_UiVisualTokens.sceneTranslucentHeaders)",
                "const float supportInset = std::max(",
                "style.FrameRounding);",
                "translucentHeaderSupportRects",
                "settingsBodyBackdrop.compositeExclusions.push_back(",
                "for (size_t backdropIndex =",
                "UiPerformanceTitleBackdropIndex;",
                "backdropIndex <= UiSettingsBodyBackdropIndex;",
                "ApplyBackdropAppearance("
            },
            "four root surfaces compose independently while the Settings body "
            "excludes authored translucent-header interiors from blur");
        const std::string_view rootBodyRounding = ExtractSection(
            viewer,
            "const float rootBodyRounding =",
            "ImGui::End();",
            "skin-aware root-panel body rounding");
        RequireOrdered(
            rootBodyRounding,
            {
                "const float rootBodyRounding =",
                "style.WindowRounding;",
                "DrawTranslucentHeaderPanelBodySurface(",
                "rootBodyRounding",
                "DrawFilledRoundedInsetFrame(",
                "settingsBodyRect,",
                "retainedSettingsContentRect,",
                "rootBodyRounding",
                "CapturePanelSurfaceBackdrops(",
                "style.FrameRounding,",
                "rootBodyRounding",
                "CapturePanelSurfaceBackdrops(",
                "style.FrameRounding,",
                "rootBodyRounding"
            },
            "root bodies and titles retain their semantic Window/Frame fields; "
            "the authored skin resolves both to its unified radius");
        const std::string_view fixedTopInsetShadow = ExtractSection(
            viewer,
            "static void DrawSettingsFixedTopInsetShadow(",
            "static bool DrawPresetResetIcon(",
            "fixed Settings top-inset shadow");
        RequireOrdered(
            fixedTopInsetShadow,
            {
                "bool intersectClipRect)",
                "const ImRect shadowRect(",
                "bodyRect.Min,",
                "bodyRect.Min.y + insetHeight",
                "const float effectiveRounding = std::max(",
                "rounding,",
                "bodyRect.GetWidth() * 0.5f - 1.f,",
                "bodyRect.GetHeight() * 0.5f - 1.f",
                "const ImRect shadowMaskRect(",
                "bodyRect.Min.y + std::max(",
                "shadowRect.GetHeight(),",
                "effectiveRounding + 1.f",
                "drawList->PushClipRect(",
                "shadowRect.Min,",
                "shadowRect.Max,",
                "intersectClipRect);",
                "drawList->AddRectFilled(",
                "shadowMaskRect.Min,",
                "shadowMaskRect.Max,",
                "effectiveRounding,",
                "ImDrawFlags_RoundCornersTop",
                "0.24f * falloff * falloff * coverage",
                "drawList->PopClipRect();"
            },
            "root panels clip a radius-safe shadow mask to their caller-owned "
            "fixed top inset without a corner wedge");
        const std::string_view filledRoundedInsetFrame = ExtractSection(
            viewer,
            "static void DrawFilledRoundedInsetFrame(",
            "static void DrawSettingsScrollEdgeFades()",
            "opaque rounded root-panel inset frame");
        RequireOrdered(
            filledRoundedInsetFrame,
            {
                "const float outerRadius = ResolveRoundedRectRadius(",
                "const float innerRadius = ResolveRoundedRectRadius(",
                "g_UiVisualTokens.panelInsetFrame",
                "(frameColor & IM_COL32_A_MASK) == 0u",
                "drawOuterSurfaceThrough =",
                "drawList->AddRectFilled(",
                "ImDrawFlags_RoundCornersAll",
                "_CalcCircleAutoSegmentCount(innerRadius)",
                "drawInnerCornerWedge =",
                "drawList->AddConcavePolyFilled(",
                "drawList->PopClipRect();"
            },
            "the shared opaque frame fills four disjoint outer strips and the "
            "four rounded inner-corner wedges without covering panel content");
        Require(
            CountOccurrences(
                filledRoundedInsetFrame,
                "drawOuterSurfaceThrough(") == 4u &&
                CountOccurrences(
                    filledRoundedInsetFrame,
                    "drawInnerCornerWedge(") == 4u,
            "the inset-frame helper must paint exactly four outer strips and "
            "four inner corner wedges.");
        RequireAbsent(
            filledRoundedInsetFrame,
            "!g_UiVisualTokens.drawControlOutlines",
            "the opaque inset frame is shared by Amp and stock Ogg even though "
            "Ogg keeps authored depth outlines disabled");
        const std::string_view settingsDecorationTracking = ExtractSection(
            viewer,
            "static bool IsSettingsChildLaterInDrawOrder(",
            "static void CaptureCurrentWindowBackdrop(",
            "final visible Settings decoration draw-list resolution");
        RequireOrdered(
            settingsDecorationTracking,
            {
                "const int popupOrder =",
                "ImGuiWindowFlags_Popup",
                "const int tooltipOrder =",
                "ImGuiWindowFlags_Tooltip",
                "candidate->BeginOrderWithinParent >",
                "current->BeginOrderWithinParent;",
                "static ImDrawList* ResolveFinalSettingsDecorationDrawList(",
                "for (ImGuiWindow* child : window->DC.ChildWindows)",
                "!child->Active || child->Hidden",
                "IsSettingsChildLaterInDrawOrder(",
                "ResolveFinalSettingsDecorationDrawList(finalVisibleChild)",
                ": window->DrawList;"
            },
            "late Settings chrome follows Dear ImGui's completed recursive "
            "visible-child render order");
        RequireAbsent(
            viewer,
            "g_SettingsDecorationDrawList",
            "retired mutable last-submitted Settings decoration owner");
        const std::string_view drawerBodyOutline = ExtractSection(
            viewer,
            "static void DrawDrawerBodyOutline(",
            "static void DrawSettingsScrollEdgeFades()",
            "drawer and Settings inner body outline");
        RequireOrdered(
            drawerBodyOutline,
            {
                "float topGap,",
                "bool intersectClipRect)",
                "const float clipTop = topGap > 0.f",
                "? outlineMinimum.y + topGap",
                ": outlineMinimum.y - Thickness;",
                "drawList->PushClipRect(",
                "clipTop),",
                "intersectClipRect);",
                "drawList->AddRect(",
                "std::max(0.f, rounding - Inset),",
                "ImDrawFlags_RoundCornersAll,",
                "Thickness);"
            },
            "drawer outlines retain all four rounded corners, preserve the "
            "zero-gap top antialias fringe, and explicitly choose whether the "
            "current child clip may trim them");
        const std::string_view performanceLateDecoration = ExtractSection(
            viewer,
            "ImDrawList* performanceWindowDrawList =",
            "const bool performanceScrollIdle =",
            "late Performance root-panel decoration");
        RequireOrdered(
            performanceLateDecoration,
            {
                "const ImRect performanceBodyRect(",
                "const ImRect performanceContentRect(",
                "if (performanceExpanded)",
                "DrawPerformancePanelContents(",
                "else",
                "performanceWindowDrawList->AddText(",
                "DrawFilledRoundedInsetFrame(",
                "performanceBodyRect,",
                "performanceContentRect,",
                "DrawSettingsFixedTopInsetShadow(",
                "performanceWindowDrawList,",
                "performanceBodyRect,",
                "std::max(",
                "performanceContentRect.Min.y -",
                "performanceBodyRect.Min.y +",
                "g_UiSpacingTokens.tight),",
                "DrawDrawerBodyOutline(",
                "performanceBodyRect.Min,",
                "performanceBodyRect.Max,",
                "DrawDrawerBodyOutline(",
                "performanceContentRect.Min,",
                "performanceContentRect.Max,"
            },
            "Performance submits its content first, then the opaque inset fill, "
            "root-owned top-margin shadow with a shallow content cast, outer "
            "outline, and inner outline");
        const std::string_view settingsChildList = ExtractSection(
            viewer,
            "ImGui::BeginChild(\n            \"##SettingsBody\"",
            "const ImVec2 settingsWindowPosition =",
            "Settings scrolling child list");
        RequireOrdered(
            settingsChildList,
            {
                "ImDrawList* settingsBodyDrawList =",
                "TrackSettingsAppearanceDrawList(settingsBodyDrawList);",
                "DrawSettingsScrollEdgeFades();",
                "const ImRect settingsBodyViewportRect(",
                "const ImRect settingsRootBodyRect(",
                "ImDrawList* settingsDecorationDrawList =",
                "ResolveFinalSettingsDecorationDrawList(settingsBodyWindow);",
                "settingsDecorationDrawList->_Splitter._Count > 1",
                "settingsDecorationDrawList->ChannelsMerge();",
                "DrawFilledRoundedInsetFrame(",
                "settingsDecorationDrawList,",
                "settingsRootBodyRect,",
                "settingsBodyViewportRect,",
                "DrawSettingsFixedTopInsetShadow(",
                "settingsDecorationDrawList,",
                "settingsRootBodyRect,",
                "std::max(",
                "settingsBodyViewportRect.Min.y -",
                "settingsRootBodyRect.Min.y +",
                "g_UiSpacingTokens.tight),",
                "style.WindowRounding,",
                "false);",
                "DrawDrawerBodyOutline(",
                "settingsDecorationDrawList,",
                "settingsRootBodyRect.Min,",
                "settingsRootBodyRect.Max,",
                "style.WindowRounding,",
                "0.f,",
                "false);",
                "DrawDrawerBodyOutline(",
                "settingsDecorationDrawList,",
                "settingsBodyViewportRect.Min,",
                "settingsBodyViewportRect.Max,",
                "style.WindowRounding,",
                "0.f,",
                "false);",
                "ImGui::PopUvsrColorPickerPopupContentRight();",
                "ImGui::EndChild();"
            },
            "the opaque inset fill, root-owned top-margin shadow with a shallow "
            "General cast, outer outline, and complete four-corner inner "
            "silhouette are appended after merging the final visible "
            "descendant's channels, immediately before EndChild");
        const std::string_view retainedSettingsDecoration = ExtractSection(
            viewer,
            "if (!settingsExpanded &&",
            "const bool settingsCollapsed =",
            "retained collapsing Settings root decoration");
        RequireOrdered(
            retainedSettingsDecoration,
            {
                "const ImRect retainedSettingsContentRect(",
                "DrawFilledRoundedInsetFrame(",
                "settingsBodyRect,",
                "retainedSettingsContentRect,",
                "DrawSettingsFixedTopInsetShadow(",
                "settingsWindowDrawList,",
                "settingsBodyRect,",
                "std::max(",
                "retainedSettingsContentRect.Min.y -",
                "settingsBodyRect.Min.y +",
                "g_UiSpacingTokens.tight),",
                "DrawDrawerBodyOutline(",
                "settingsBodyRect.Min,",
                "settingsBodyRect.Max,",
                "DrawDrawerBodyOutline(",
                "retainedSettingsContentRect.Min,",
                "retainedSettingsContentRect.Max,"
            },
            "the retained collapsing Settings body preserves the same fill, "
            "root-margin shadow geometry, outer-outline, and inner-outline "
            "order");
        for (const std::string_view retiredBackdropContract : {
                std::string_view("UiPanelStackBackdropIndex"),
                std::string_view("UiPanelStackShadowBackdropIndex"),
                std::string_view("UiPanelSeamBackdropIndex"),
                std::string_view("CaptureStackedPanelBackdrop("),
                std::string_view("CaptureStackedPanelBackdrops("),
                std::string_view("CaptureStackedPanelSeamBackdrop") })
        {
            RequireAbsent(
                viewer,
                retiredBackdropContract,
                "retired union-stack backdrop contract");
        }
        const std::string_view backdropRenderRouting = ExtractSection(
            viewer,
            "const bool hasVisibleBackdrop = std::any_of(",
            "m_CommandList->endMarker();",
            "backdrop exclusion render routing");
        RequireOrdered(
            backdropRenderRouting,
            {
                "ResolveBackdropCompositeRegions(",
                "backdropRect.compositeExclusions",
                "for (const UiBackdropExclusionRect& region : regions)",
                "compositeState.viewport.addScissorRect("
            },
            "Settings-body blur excludes authored translucent-header interiors");
        const std::string_view backdropConstants = ExtractSection(
            viewer,
            "struct alignas(16) BackdropBlurConstants",
            "class BackdropBlurPass",
            "CPU backdrop blur constant layout");
        RequireOrdered(
            backdropConstants,
            {
                "float shadowOffsetY = 0.f;",
                "uint32_t cornerMask = UiBackdropCornersAll;",
                "float2 padding;",
                "static_assert(sizeof(BackdropBlurConstants) == 80u);"
            },
            "CPU backdrop corner-mask constant layout");
        RequireContains(
            viewer,
            "constants.cornerMask = backdropRect.cornerMask;",
            "backdrop corner-mask upload");
        RequireOrdered(
            backdropBlurShader,
            {
                "uint cornerMask;",
                "float ResolveCornerRadius(",
                "? (right ? 0x8u : 0x4u)",
                ": (right ? 0x2u : 0x1u);",
                "g_BackdropBlur.cornerMask"
            },
            "shader-side selective rounded-corner mask");

        const std::string_view skinApplication = ExtractSection(
            viewer,
            "static void ApplyUiSkin(",
            "static void PushPanelBodySurface()",
            "UI skin application");
        RequireOrdered(
            skinApplication,
            {
                "colors[ImGuiCol_Border] =\n"
                "                ImVec4(0.15f, 0.15f, 0.15f, 0.92f);",
                "colors[ImGuiCol_BorderShadow] =\n"
                "                ImVec4(0.01f, 0.012f, 0.016f, 0.48f);"
            },
            "authored BorderShadow immediately follows Border with enough alpha "
            "to keep the borderless picker swatch shadow visible");
        RequireOrdered(
            skinApplication,
            {
                "bool animationsEnabled,",
                "ImGui::SetUvsrUiBehavior(",
                "ResolveUiMotionEnabled(",
                "resolvedSkin,",
                "animationsEnabled),"
            },
            "authored motion policy combines the skin and live Interface "
            "animation preference");
        Require(
            CountOccurrences(viewer, "ResolveUiMotionEnabled(") == 2u,
            "skin application and renderer transitions must share one "
            "effective motion resolver");
        RequireOrdered(
            viewer,
            {
                "const bool uiMotionEnabled =",
                "ResolveUiMotionEnabled(",
                "m_ui.Skin,",
                "m_ui.AnimationsEnabled);",
                "ApplyUiSkin(",
                "m_ComposedUiSkin,",
                "m_ui.Accents,",
                "m_ui.AnimationsEnabled,"
            },
            "renderer and composed ImGui motion both observe the same live "
            "Interface preference");
        const std::string_view uiColorHelpers = ExtractSection(
            viewer,
            "static ImVec4 MakeUiColor(",
            "static void ApplyUiSkin(",
            "RGBA UI color helpers");
        Require(
            CountOccurrences(
                uiColorHelpers,
                "color.alpha * alphaMultiplier") == 3u,
            "direct, scaled, and offset UI colors must all propagate the "
            "authored alpha multiplier.");
        RequireContains(
            uiColorHelpers,
            "return luminance >= 0.68f;",
            "structural scene-translucency remains active for an authored "
            "ultra-bright Primary Accent RGB even at transparent alpha");
        const std::string_view scrollEdgeFade = ExtractSection(
            viewer,
            "static void DrawSettingsScrollEdgeFades()",
            "static void EndDrawerBody()",
            "Settings scroll-edge alpha routing");
        RequireOrdered(
            scrollEdgeFade,
            {
                "ImVec4 edgeColor = style.Colors[ImGuiCol_WindowBg];",
                "ImVec4 clearColor = edgeColor;",
                "clearColor.w = 0.f;",
                "color.w *= std::clamp("
            },
            "scroll-edge fades derive from actual panel alpha down to a "
            "transparent endpoint");
        RequireAbsent(
            scrollEdgeFade,
            "std::max(edgeColor.w",
            "retired scroll-fade alpha floor");
        for (const std::string_view paletteContract : {
                std::string_view("const UiSkinPalette* storedPalette ="),
                std::string_view(
                    "FindUiSkinPalette(accents, resolvedSkin);"),
                std::string_view("const UiSkinPalette* defaultPalette ="),
                std::string_view(
                    "FindDefaultUiSkinPalette(resolvedSkin);"),
                std::string_view("const UiSkinPalette& palette = storedPalette"),
                std::string_view(
                    "const bool authoredSkin = resolvedSkin != UiSkin::Og;"),
                std::string_view(
                    "IsUltraBrightUiColor(palette.primaryAccent);"),
                std::string_view(
                    "const bool brightPrimaryBackground = authoredSkin &&"),
                std::string_view(
                    "IsUltraBrightUiColor(palette.primaryBackground);"),
                std::string_view(
                    "const bool sceneTranslucentHeaders = authoredSkin &&"),
                std::string_view(
                    "brightPrimaryAccent;"),
                std::string_view(
                    "colors[ImGuiCol_Text] = MakeUiColor(palette.fontColor);"),
                std::string_view(
                    "colors[ImGuiCol_FrameBg] =\n"
                    "                MakeUiColor(palette.primaryBackground);"),
                std::string_view(
                    "const ImVec4 opaquePrimaryAccent("),
                std::string_view(
                    "colors[ImGuiCol_CheckMark] = opaquePrimaryAccent;"),
                std::string_view(
                    "tokens.drawerHeader =\n"
                    "                MakeUiColor(palette.primaryAccent);"),
                std::string_view(
                    "ScaleUiColor(\n"
                    "                    palette.primaryAccent,\n"
                    "                    0.82f,\n"
                    "                    0.48f / 0.31f);"),
                std::string_view(
                    "ScaleUiColor(\n"
                    "                    palette.primaryAccent,\n"
                    "                    0.70f,\n"
                    "                    0.65f / 0.31f);"),
                std::string_view(
                    "tokens.drawerHeaderText =\n"
                    "                MakeUiColor(palette.fontColor);"),
                std::string_view(
                    "tokens.drawerBackground =\n"
                    "                ImVec4(\n"
                    "                    0.66f,\n"
                    "                    0.67f,\n"
                    "                    0.69f,\n"
                    "                    std::clamp(\n"
                    "                        palette.primaryBackground.alpha *\n"
                    "                            (0.13f / SecondaryRestAlpha),\n"
                    "                        0.f,\n"
                    "                        1.f));"),
                std::string_view(
                    "tokens.panelBodySurface = MakeUiColor(\n"
                    "                palette.primaryBackground,"),
                std::string_view(
                    "tokens.colorPickerSurface = tokens.panelBodySurface;"),
                std::string_view(
                    "tokens.panelInsetFrame = ImVec4(\n"
                    "                tokens.panelBodySurface.x,\n"
                    "                tokens.panelBodySurface.y,\n"
                    "                tokens.panelBodySurface.z,\n"
                    "                1.f);"),
                std::string_view(
                    "tokens.settingsTitleText =\n"
                    "                MakeUiColor(palette.fontColor);"),
                std::string_view(
                    "tokens.actionButtonText =\n"
                    "                MakeUiColor(palette.fontColor);"),
                std::string_view(
                    "tokens.sceneTranslucentHeaders =\n"
                    "                sceneTranslucentHeaders;") })
        {
            RequireContains(
                skinApplication,
                paletteContract,
                "Amp RGBA role routing and ultra-bright custom transmission");
        }
        RequireContains(
            skinApplication,
            "tokens.colorPickerSurface = colors[ImGuiCol_PopupBg];",
            "Ogg retains the stock popup background for its picker surface");
        RequireContains(
            skinApplication,
            "tokens.panelInsetFrame = ImVec4(\n"
            "                colors[ImGuiCol_WindowBg].x,\n"
            "                colors[ImGuiCol_WindowBg].y,\n"
            "                colors[ImGuiCol_WindowBg].z,\n"
            "                1.f);",
            "Ogg also supplies an opaque inset-frame color from stock WindowBg");
        Require(
            CountOccurrences(
                skinApplication,
                "tokens.panelInsetFrame = ImVec4(") == 2u,
            "authored Amp and stock Ogg must share exactly one opaque inset-frame "
            "token assignment each.");
        RequireOrdered(
            skinApplication,
            {
                "tokens.drawerHeader =",
                "tokens.drawerHeaderHovered =",
                "tokens.drawerHeaderActive ="
            },
            "Amp drawer resting-hover-active token order");
        RequireOrdered(
            skinApplication,
            {
                "colors[ImGuiCol_FrameBgHovered] = brightPrimaryBackground",
                "? ScaleUiColor(\n"
                "                    palette.primaryBackground,\n"
                "                    0.82f,\n"
                "                    0.76f / SecondaryRestAlpha)",
                ": OffsetUiColor(\n"
                "                    palette.primaryBackground,\n"
                "                    0.112f,\n"
                "                    0.76f / SecondaryRestAlpha);",
                "colors[ImGuiCol_FrameBgActive] = brightPrimaryBackground",
                "? ScaleUiColor(\n"
                "                    palette.primaryBackground,\n"
                "                    0.70f,\n"
                "                    0.82f / SecondaryRestAlpha)",
                ": OffsetUiColor(\n"
                "                    palette.primaryBackground,\n"
                "                    0.162f,\n"
                "                    0.82f / SecondaryRestAlpha);"
            },
            "ultra-bright closed controls darken with calibrated alpha while "
            "dark backgrounds retain positive hover and active offsets");
        RequireOrdered(
            skinApplication,
            {
                "if (brightPrimaryAccent)",
                "tokens.drawerHeaderHovered = ScaleUiColor(\n"
                    "                    palette.primaryAccent,\n"
                    "                    0.82f,\n"
                    "                    0.48f / 0.31f);",
                "tokens.drawerHeaderActive = ScaleUiColor(\n"
                    "                    palette.primaryAccent,\n"
                    "                    0.70f,\n"
                    "                    0.65f / 0.31f);",
                "else",
                "tokens.drawerHeaderHovered = MakeUiColor(\n"
                    "                    palette.primaryAccent,\n"
                    "                    0.48f / 0.31f);",
                "tokens.drawerHeaderActive = MakeUiColor(\n"
                    "                    palette.primaryAccent,\n"
                    "                    0.65f / 0.31f);"
            },
            "ultra-bright custom Amp Primary Accents darken while ordinary Amp "
            "accents retain the authored alpha curve");
        Require(
            CountOccurrences(
                skinApplication,
                "brightPrimaryBackground") == 3u,
            "bright Primary Background detection must feed exactly the two "
            "closed-control ternaries");
        RequireOrdered(
            skinApplication,
            {
                "tokens.actionButton = tokens.drawerHeader;",
                "tokens.actionButtonHovered =\n"
                "                tokens.drawerHeaderHovered;",
                "tokens.actionButtonActive =\n"
                "                tokens.drawerHeaderActive;",
                "colors[ImGuiCol_Header] = tokens.drawerHeader;",
                "colors[ImGuiCol_HeaderHovered] =",
                "tokens.drawerHeaderHovered;",
                "colors[ImGuiCol_HeaderActive] =",
                "tokens.drawerHeaderActive;"
            },
            "Primary Accent routes through authored drawer headers, footer "
            "actions, slider grabs, and selected/hovered popup rows");
        RequireOrdered(
            skinApplication,
            {
                "tokens.settingsTitleSurface = sceneTranslucentHeaders",
                "? tokens.drawerHeader",
                ": CompositeUiColorOver(",
                "tokens.drawerHeader,",
                "tokens.panelBodySurface);"
            },
            "an ultra-bright custom Amp title preserves raw scene-transmitting "
            "Primary Accent while the ordinary title is precomposited");
        const std::string_view translucentBodySurface = ExtractSection(
            viewer,
            "static void DrawTranslucentHeaderPanelBodySurface(",
            "static const char* GetDeferredDropdownPreview(",
            "translucent-header Settings body support complement");
        RequireOrdered(
            translucentBodySurface,
            {
                "std::vector<ImRect> exclusions =",
                "translucentHeaderSupportRects;",
                "const float headerSupportInset = std::max(",
                "1.f,",
                "ImGui::GetStyle().FrameRounding);",
                "for (ImRect exclusion : exclusions)",
                "exclusion.Min.x += headerSupportInset;",
                "exclusion.Min.y += headerSupportInset;",
                "exclusion.Max.x -= headerSupportInset;",
                "exclusion.Max.y -= headerSupportInset;",
                "nextFullWidthY = exclusion.Max.y;",
                "ImGui::RenderFrameBorder("
            },
            "ultra-bright custom Amp Settings body excludes rounding-scaled header "
            "support rectangles");
        Require(
            CountOccurrences(
                translucentBodySurface,
                "drawClippedSurface(ImRect(") == 4u,
            "the authored body complement must paint above, beside, and after each "
            "translucent header support rectangle.");
        const std::string_view translucentSettingsWindow = ExtractSection(
            viewer,
            "ImGuiWindowFlags settingsWindowFlags =",
            "const bool settingsCollapsed =",
            "ultra-bright authored Settings root surface routing");
        RequireOrdered(
            translucentSettingsWindow,
            {
                "if (g_UiVisualTokens.sceneTranslucentHeaders)",
                "settingsWindowFlags |= ImGuiWindowFlags_NoBackground;",
                ".translucentHeaderSupportRects.clear();",
                "if (settingsExpanded &&",
                "g_UiVisualTokens.sceneTranslucentHeaders)",
                "DrawTranslucentHeaderPanelBodySurface("
            },
            "ultra-bright custom Amp raw title and body-complement rendering");
        const std::string_view footerSceneTransmission = ExtractSection(
            viewer,
            "const ImVec2 actionButtonRowMinimum =",
            "ImGui::PopStyleColor(4);",
            "translucent footer support exclusion");
        RequireOrdered(
            footerSceneTransmission,
            {
                "const ImVec2 actionButtonRowMinimum =",
                "const ImVec2 actionButtonRowMaximum =",
                "if (g_UiVisualTokens.sceneTranslucentHeaders)",
                ".translucentHeaderSupportRects.push_back(",
                "actionButtonRowMinimum,",
                "actionButtonRowMaximum"
            },
            "an ultra-bright authored action row also reveals unblurred scene "
            "content through its support exclusion");
        const std::string_view translucentRootSeamSupport = ExtractSection(
            imguiUiOverride,
            "expanded_stacked_panel_body_rect = ImRect(",
            "// Title bar",
            "ultra-bright authored Settings title/body seam support");
        RequireOrdered(
            translucentRootSeamSupport,
            {
                "window->TitleBarHeight - 1.0f",
                "AddRectFilled",
                "ImDrawFlags_RoundCornersAll"
            },
            "ultra-bright authored root keeps a one-pixel fully rounded "
            "title/body support overlap");

        for (const std::string_view boldFontContract : {
                std::string_view("UVSR_UI_BOLD_FONT_SOURCE"),
                std::string_view("$ENV{WINDIR}/Fonts/segoeuib.ttf"),
                std::string_view("CodexUI-Bold.ttf") })
        {
            RequireContains(
                cmakeSource,
                boldFontContract,
                "staged bold authored-header font");
        }
        RequireContains(
            viewer,
            "std::shared_ptr<app::RegisteredFont> m_HeaderFont;",
            "owned authored-header font");
        RequireContains(
            viewer,
            "m_HeaderFont = CreateFontFromFile(",
            "loaded authored-header font");
        RequireContains(
            viewer,
            "\"/media/fonts/System/CodexUI-Bold.ttf\"",
            "runtime authored-header font path");
        const std::string_view collapsingHeader = ExtractSection(
            viewer,
            "bool DrawCollapsingHeader(",
            "static void BeginDrawerBody(",
            "bold collapsing-header typography");
        RequireOrdered(
            collapsingHeader,
            {
                "m_ComposedUiSkin != UiSkin::Og",
                "ImGui::PushFont(GetActiveUiHeaderFont())",
                "ApplyActiveUiHeaderWordSpacing();",
                "ImGui::CollapsingHeader(label, flags)",
                "RestoreActiveUiHeaderWordSpacing();",
                "ImGui::PopFont();"
            },
            "bold authored collapsing-header typography");
        Require(
            CountOccurrences(
                viewer,
                "ImGui::PushFont(GetActiveUiHeaderFont())") == 3u,
            "the Performance title, Settings title, and authored drawers must "
            "each push the bold header font exactly once.");
        const std::string_view performanceTitle = ExtractSection(
            viewer,
            "ImGuiWindowFlags performanceWindowFlags =",
            "ImDrawList* performanceWindowDrawList =",
            "bold Performance title");
        RequireOrdered(
            performanceTitle,
            {
                "ImGui::PushFont(GetActiveUiHeaderFont())",
                "ApplyActiveUiHeaderWordSpacing();",
                "ImGui::Begin(\n            \"Performance\"",
                "RestoreActiveUiHeaderWordSpacing();",
                "ImGui::PopFont();"
            },
            "bold authored Performance title");
        const std::string_view settingsTitle = ExtractSection(
            viewer,
            "ImGuiWindowFlags settingsWindowFlags =",
            "ImDrawList* settingsWindowDrawList =",
            "bold Settings title");
        RequireOrdered(
            settingsTitle,
            {
                "ImGui::PushFont(GetActiveUiHeaderFont())",
                "ApplyActiveUiHeaderWordSpacing();",
                "ImGui::Begin(\n            \"Settings\"",
                "RestoreActiveUiHeaderWordSpacing();",
                "ImGui::PopFont();"
            },
            "bold authored Settings title");

        const std::string_view layoutAnimationStep = ExtractSection(
            viewer,
            "UiLayoutAnimationDurationSeconds = 0.18f;",
            "static float GetCommandInterfaceMinimumHeight()",
            "authored drawer animation step");
        RequireOrdered(
            layoutAnimationStep,
            {
                "std::max(0.f, ImGui::GetIO().DeltaTime)",
                "1.f / 30.f",
                "animationDeltaTime /",
                "UiLayoutAnimationDurationSeconds"
            },
            "authored drawer large-delta clamp");
        const std::string_view advanceLayoutAnimation = ExtractSection(
            viewer,
            "static float AdvanceUiLayoutAnimation(",
            "static float SmoothUiLayoutAnimation(",
            "authored drawer animation advance");
        RequireOrdered(
            advanceLayoutAnimation,
            {
                "if (!ImGui::IsUvsrUiMotionEnabled())",
                "return targetVisible ? 1.f : 0.f;",
                "const float step = GetUiLayoutAnimationStep();",
                "std::min(1.f, amount + step)",
                "std::max(0.f, amount - step)"
            },
            "Ogg-immediate and reversible authored drawer advance");
        RequireOrdered(
            collapsingHeader,
            {
                "if (!ImGui::IsUvsrUiMotionEnabled())",
                "openAmount = open ? 1.f : 0.f;",
                "else if (lastFrame < frame - 1)",
                "ResolveUiOpenAmountAfterSubmissionGap(",
                "AdvanceUiLayoutAnimation(openAmount, open)"
            },
            "Ogg drawer endpoint resolves before submission-gap animation");
        RequireOrdered(
            collapsingHeader,
            {
                "bool forceClosedPresentation = false",
                "const int lastFrame = forceClosedPresentation",
                "? frame - 2",
                "const bool previousTargetOpen = forceClosedPresentation",
                "? false",
                "float openAmount = forceClosedPresentation",
                "? 0.f",
                "else if (lastFrame < frame - 1)",
                "ResolveUiOpenAmountAfterSubmissionGap("
            },
            "forced Material reset seeds a false-to-true submission gap from "
            "the closed endpoint");
        const std::string_view nestedDrawer = ExtractSection(
            viewer,
            "static bool BeginAnimatedTreeNode(",
            "static void EndAnimatedTreeNode()",
            "nested authored drawer animation");
        RequireOrdered(
            nestedDrawer,
            {
                "if (!ImGui::IsUvsrUiMotionEnabled())",
                "openAmount = open ? 1.f : 0.f;",
                "else if (lastFrame < frame - 1)",
                "ResolveUiOpenAmountAfterSubmissionGap(",
                "AdvanceUiLayoutAnimation(openAmount, open)"
            },
            "Ogg nested-drawer endpoint resolves before submission-gap animation");
        RequireOrdered(
            collapsingHeader,
            {
                "ImGui::BeginUvsrTreeArrowCapture();",
                "ImGui::CollapsingHeader(label, flags)",
                "storage->SetFloat(amountKey, openAmount);",
                "ImGui::EndUvsrTreeArrowCapture(",
                "SmoothUiLayoutAnimation(openAmount),",
                "ImGuiTreeNodeFlags_UpsideDownArrow"
            },
            "top-level drawer arrows rotate from the same reversible eased "
            "timeline as their bodies");
        RequireOrdered(
            nestedDrawer,
            {
                "ImGui::BeginUvsrTreeArrowCapture();",
                "ImGui::TreeNodeEx(",
                "storage->SetFloat(amountKey, openAmount);",
                "ImGui::EndUvsrTreeArrowCapture(",
                "SmoothUiLayoutAnimation(openAmount),",
                "ImGuiTreeNodeFlags_UpsideDownArrow"
            },
            "nested drawer arrows rotate from the same reversible eased "
            "timeline as their bodies");
        const std::string_view rootCollapseOverride = ExtractSection(
            imguiUiOverride,
            "float uvsr_window_collapse_amount = -1.0f;",
            "// Apply minimum/maximum window size constraints and final size",
            "generic authored root collapse animation");
        RequireOrdered(
            rootCollapseOverride,
            {
                "if (last_frame < g.FrameCount - 1)",
                "start_amount = collapse_amount;",
                "if (window->WantCollapseToggle)",
                "start_amount = collapse_amount;",
                "uvsr_window_collapse_target >= 0",
                "start_amount = collapse_amount;",
                "constexpr float MaximumCollapseDelta = 1.0f / 30.0f;",
                "ImMin(",
                "ImMax(0.0f, g.IO.DeltaTime)",
                "MaximumCollapseDelta",
                "collapse_amount = ImLerp("
            },
            "generic root submission-gap, reversal, and delta-clamp contract");
        RequireOrdered(
            rootCollapseOverride,
            {
                "const float target_amount =",
                "if (!g.UvsrUiMotionEnabled)",
                "collapse_amount = target_amount;",
                "start_amount = target_amount;",
                "elapsed = 0.0f;",
                "else if (collapse_amount != target_amount)"
            },
            "the animation master switch snaps an in-flight authored root "
            "collapse to its target without taking the Ogg stock path");
        Require(
            CountOccurrences(
                imguiUiOverride,
                "UvsrWindowCollapseAmountKeySalt") == 3u,
            "the authored-root collapse renderer and current-window query must "
            "share one storage-key salt.");
        const std::string_view rootCollapseQuery = ExtractSection(
            imguiUiOverride,
            "bool ImGui::IsCurrentUvsrWindowCollapseTransitionActive()",
            "void ImGui::SetNextWindowBgAlpha(",
            "current authored-root transition query");
        RequireOrdered(
            rootCollapseQuery,
            {
                "ImGuiWindow* window = g.CurrentWindow;",
                "window == NULL",
                "window->LastFrameActive != g.FrameCount",
                "g.UvsrStockWidgetRendering",
                "window->ID ^ UvsrWindowCollapseAmountKeySalt",
                "collapse_amount > 0.0f && collapse_amount < 1.0f"
            },
            "active current authored-root transition query");
        for (const std::string_view publicCollapseApi : {
                std::string_view(
                    "SetNextUvsrWindowCollapsedHeight(float collapsed_height);"),
                std::string_view(
                    "SetNextUvsrWindowCollapseTarget(bool collapsed);"),
                std::string_view(
                    "IsCurrentUvsrWindowCollapseTransitionActive();") })
        {
            RequireContains(
                imguiUiOverride,
                publicCollapseApi,
                "generic authored-root collapse API");
        }
        Require(
            CountOccurrences(
                viewer,
                "ImGui::SetNextUvsrWindowCollapsedHeight(") == 2u &&
            CountOccurrences(
                viewer,
                "ImGui::IsCurrentUvsrWindowCollapseTransitionActive();") == 2u,
            "Performance and Settings must both use and query generic root motion");
        const std::string_view rootCompletionBarrier = ExtractSection(
            viewer,
            "const bool performanceCollapseTransitionActive =",
            "RestoreActiveUiWordSpacing();",
            "root transition commit barrier");
        RequireOrdered(
            rootCompletionBarrier,
            {
                "const bool performanceCollapseTransitionActive =",
                "ImGui::IsCurrentUvsrWindowCollapseTransitionActive();",
                "const bool settingsCollapseTransitionActive =",
                "ImGui::IsCurrentUvsrWindowCollapseTransitionActive();",
                "settingsLayoutIdle =",
                "settingsLayoutIdle &&",
                "!settingsCollapseTransitionActive;",
                "settingsLayoutIdle &&",
                "!performanceCollapseTransitionActive &&",
                "!g_PerformanceTableTransitionActive"
            },
            "deferred dropdown barrier includes both root transitions and the "
            "Performance table-height exchange");
        RequireAbsent(
            viewer,
            "ImGui::GetCursorPosY() - style.WindowPadding.y",
            "retired Settings root top-padding cancellation");

        const std::string_view visualDisabledScopes = ExtractSection(
            viewer,
            "struct UiDisabledPresentationState",
            "static bool BeginAnimatedToggleRegion(",
            "nested-safe visual disabled scopes");
        RequireOrdered(
            visualDisabledScopes,
            {
                "float linearAmount = 0.f;",
                "bool initialized = false;",
                "int lastSeenFrame = -1;",
                "int advancedFrame = -1;",
                "g_UiDisabledPresentationStates;",
                "g_UiVisualDisabledScopeDepth = 0;",
                "static float ResolveDisabledPresentationAmount(",
                "const bool submissionWasInterrupted =",
                "state.linearAmount = disabled ? 1.f : 0.f;",
                "AdvanceUiDisabledPresentation(",
                "ImGui::GetIO().DeltaTime,",
                "motionEnabled);",
                "return SmoothUiDisabledPresentation(state.linearAmount);",
                "static bool BeginVisuallyDisabledUiScope(",
                "const char* id,",
                "bool disabled)",
                "g_UiDisabledPresentationStates[ImGui::GetID(id)]",
                "ResolveDisabledPresentationAmount(state, disabled);",
                "g_UiVisualDisabledScopeDepth == 0;",
                "ImGuiStyleVar_Alpha",
                "g_UiVisualTokens.controlDisabledAlpha - 1.f",
                "presentationAmount",
                "ImGuiStyleVar_DisabledAlpha",
                "1.f);",
                "ImGui::PushUvsrDisabledPresentation(",
                "presentationAmount);",
                "ImGui::BeginDisabled(disabled);",
                "++g_UiVisualDisabledScopeDepth;",
                "static void EndVisuallyDisabledUiScope(bool manualAlphaApplied)",
                "--g_UiVisualDisabledScopeDepth;",
                "ImGui::EndDisabled();",
                "ImGui::PopUvsrDisabledPresentation();",
                "ImGui::PopStyleVar();",
                "if (manualAlphaApplied)",
                "ImGui::PopStyleVar();"
            },
            "per-ID gated presentation advances monotonically over the shared "
            "timeline, while semantic disabling applies immediately and nested "
            "scopes apply one visual multiplier");
        RequireContains(
            uiAnimation,
            "UiDisabledPresentationDurationSeconds = 0.280f;",
            "280 ms authored disabled-presentation duration");
        RequireOrdered(
            imguiUiOverride,
            {
                "static void ApplyDisabledGrayscale(ImVec4& color)",
                "g.UvsrDisabledPresentationAmountStack.Size > 0",
                "g.UvsrDisabledPresentationAmountStack.back()",
                "ImLerp(color.x, luminance, amount)",
                "GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled",
                "GImGui->UvsrDisabledPresentationAmountStack.Size > 0",
                "GImGui->UvsrDisabledPresentationAmountStack.back() > 0.0f",
                "ApplyDisabledGrayscale(c);",
                "void ImGui::PushUvsrDisabledPresentation(float amount)",
                "const float inherited =",
                "ImMax(inherited, ImSaturate(amount))",
                "void ImGui::PopUvsrDisabledPresentation()"
            },
            "the ImGui disabled-presentation scope is nested-safe, including "
            "packed colors during re-enable fades, and drives authored grayscale "
            "independently from semantic BeginDisabled");

        const std::string_view toggleRegion = ExtractSection(
            viewer,
            "static bool BeginAnimatedToggleRegion(",
            "static void EndAnimatedToggleRegion()",
            "animated toggle-region visual gating");
        RequireOrdered(
            toggleRegion,
            {
                "state.disabledPresentationLinearAmount =",
                "visible ? 0.f : 1.f;",
                "if (!motionEnabled)",
                "state.disabledPresentationLinearAmount =",
                "visible ? 0.f : 1.f;",
                "else if (state.disabledPresentationAdvancedFrame != frame)",
                "AdvanceUiDisabledPresentation(",
                "!state.targetVisible,",
                "ImGui::GetIO().DeltaTime,",
                "const float disabledPresentationAmount =",
                "SmoothUiDisabledPresentation(",
                "ImGuiStyleVar_Alpha",
                "easedAmount",
                "g_UiVisualTokens.controlDisabledAlpha",
                "disabledPresentationAmount",
                "ImGuiStyleVar_DisabledAlpha",
                "1.f);",
                "ImGui::PushUvsrDisabledPresentation(",
                "disabledPresentationAmount);",
                "ImGui::BeginDisabled(",
                "!state.targetVisible || state.linearAmount < 1.f",
                "++g_UiVisualDisabledScopeDepth;"
            },
            "toggle regions use the same 280 ms presentation amount while "
            "blocking interaction immediately throughout opening and closing");
        RequireContains(
            Compact(toggleRegion),
            "constbooltransitionActive=targetChangedThisFrame||"
            "needsInitialMeasurement||(state.linearAmount>0.f&&"
            "state.linearAmount<1.f);",
            "toggle transition activity excludes the steady hidden endpoint");
        RequireOrdered(
            toggleRegion,
            {
                "const bool transitionActive =",
                "targetChangedThisFrame ||",
                "needsInitialMeasurement ||",
                "(state.linearAmount > 0.f &&",
                "state.linearAmount < 1.f);",
                "if (owner == UiToggleRegionOwner::Settings)",
                "if (transitionActive)",
                "MarkSettingsLayoutAnimationActive();",
                "else if (transitionActive)",
                "g_PerformanceTableTransitionActive = true;"
            },
            "only target changes, initial measurement, and strictly interior "
            "progress hold the Settings or Performance commit barrier");
        RequireAbsent(
            Compact(toggleRegion),
            "constbooltransitionActive=targetChangedThisFrame||"
            "state.linearAmount<1.f;",
            "regressed always-active hidden Performance transition predicate");
        const std::string_view toggleRegionEnd = ExtractSection(
            viewer,
            "static void EndAnimatedToggleRegion()",
            "static float GetUiHighlightFade(",
            "animated toggle-region disabled-scope unwind");
        RequireOrdered(
            toggleRegionEnd,
            {
                "if (context.ownsDisabledPresentationScope)",
                "assert(g_UiVisualDisabledScopeDepth > 0);",
                "--g_UiVisualDisabledScopeDepth;",
                "ImGui::EndDisabled();",
                "ImGui::PopUvsrDisabledPresentation();"
            },
            "toggle-region disabled presentation unwinds after its immediate "
            "semantic item scope");
        RequireAbsent(
            viewer,
            "FreezeAnimatedToggleVisualValues",
            "retired frozen toggle and slider presentation cache");
        const std::string_view rootInputLocks = ExtractSection(
            viewer,
            "const bool deferredDropdownInputBlocked =",
            "EndSettingsScrollStability();",
            "input-only Settings root locks");
        Require(
            CountOccurrences(
                rootInputLocks,
                "ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f);") == 2u,
            "deferred-action and scroll-stability root locks must suppress "
            "input without visually dimming Settings.");
        Require(
            CountOccurrences(
                rootInputLocks,
                "ImGui::PushUvsrDisabledPresentation(0.f);") == 2u &&
                CountOccurrences(
                    rootInputLocks,
                    "ImGui::PopUvsrDisabledPresentation();") == 2u,
            "both root input-only locks explicitly preserve a zero authored "
            "disabled-presentation amount and unwind it exactly once");

        const std::string_view sliderWrapper = ExtractSection(
            viewer,
            "bool DrawBoundedSliderFloat(",
            "static bool DrawCenteredActionButton(",
            "logical and pointer-travel slider submission");
        RequireOrdered(
            sliderWrapper,
            {
                "float logicalMinimum,",
                "float logicalMaximum,",
                "float travelMinimum,",
                "float travelMaximum,",
                "const ImGuiSliderFlags effectiveFlags =",
                "flags |",
                "(m_ui.OverrideVisualMaxes",
                "? ImGuiSliderFlags_None",
                ": ImGuiSliderFlags_AlwaysClamp);",
                "ImGui::SliderFloat(",
                "label,",
                "value,",
                "travelMinimum,",
                "travelMaximum,",
                "format,",
                "effectiveFlags);",
                "if (changed)",
                "*value = std::clamp(",
                "logicalMinimum,",
                "logicalMaximum);",
                "bool DrawSliderFloat(",
                "return DrawBoundedSliderFloat(",
                "bool DrawBoundedSliderInt(",
                "const ImGuiSliderFlags effectiveFlags =",
                "flags |",
                "(m_ui.OverrideVisualMaxes",
                "? ImGuiSliderFlags_None",
                ": ImGuiSliderFlags_AlwaysClamp);",
                "ImGui::SliderInt(",
                "effectiveFlags);",
                "if (changed)",
                "*value = std::clamp(",
                "logicalMinimum,",
                "logicalMaximum);",
                "bool DrawSliderInt(",
                "return DrawBoundedSliderInt("
            },
            "slider wrappers add AlwaysClamp only while visual-max override is "
            "off, preserve caller flags, and post-clamp accepted edits to the "
            "logical safety range");
        const std::string compactSliderWrapper = Compact(sliderWrapper);
        Require(
            CountOccurrences(
                compactSliderWrapper,
                "constImGuiSliderFlagseffectiveFlags=flags|"
                "(m_ui.OverrideVisualMaxes?ImGuiSliderFlags_None:"
                "ImGuiSliderFlags_AlwaysClamp);") == 2u,
            "both slider wrappers must retain caller AlwaysClamp while "
            "conditionally adding the default visual-track clamp.");
        Require(
            CountOccurrences(
                compactSliderWrapper,
                "*value=std::clamp(*value,logicalMinimum,logicalMaximum);") ==
                2u,
            "both slider wrappers must enforce logical limits after exact "
            "numeric input.");
        for (const std::string_view retiredSliderCache : {
                std::string_view("StateStorage"),
                std::string_view("unordered_map"),
                std::string_view("presentation") })
        {
            RequireAbsent(
                sliderWrapper,
                retiredSliderCache,
                "retired UVSR-side slider presentation cache");
        }
        const std::string compactSoftRangeSource = Compact(viewer);
        for (const std::string_view softRangeCall : {
                std::string_view(
                    "DrawBoundedSliderFloat(\"Strength\","
                    "&visibility.ambientOcclusion.strength,"
                    "MinimumVisibilityAmbientOcclusionStrength,"
                    "MaximumVisibilityAmbientOcclusionStrength,"
                    "MinimumVisibilityAmbientOcclusionStrength,2.f,\"%.2f\")"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"Intensity\","
                    "&visibility.indirectDiffuse.intensity,"
                    "0.f,16.f,0.f,4.f,\"%.2f\")"),
                std::string_view(
                    "DrawBoundedSliderInt(\"Samples\",&samples,"
                    "1,64,1,48)"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"Radius\","
                    "&visibility.sampling.radius,"
                    "0.1f,10.f,0.1f,6.f,\"%.2f\")"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"Thickness\","
                    "&visibility.sampling.thickness,"
                    "0.01f,2.f,0.01f,1.f,\"%.2f\")"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"Distribution\","
                    "&visibility.sampling.stepDistributionExponent,"
                    "MinimumVisibilityStepDistributionExponent,"
                    "MaximumVisibilityStepDistributionExponent,"
                    "MinimumVisibilityStepDistributionExponent,4.f,\"%.2f\")"),
                std::string_view(
                    "DrawBoundedSliderInt(\"HistoryFrames\","
                    "&historyFrames,1,32,1,16)"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"ExposureCompensation\","
                    "&m_ui.AutoExposure.exposureCompensationEV,"
                    "AutoExposureMinimumCompensationEV,"
                    "AutoExposureMaximumCompensationEV,-8.f,8.f,\"%+.2fEV\")"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"MaximumBrightening\","
                    "&m_ui.AutoExposure.maximumBrighteningEV,"
                    "AutoExposureMinimumMovementEV,"
                    "AutoExposureMaximumMovementEV,"
                    "AutoExposureMinimumMovementEV,8.f,\"%.2fEV\")"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"MaximumDarkening\","
                    "&m_ui.AutoExposure.maximumDarkeningEV,"
                    "AutoExposureMinimumMovementEV,"
                    "AutoExposureMaximumMovementEV,"
                    "AutoExposureMinimumMovementEV,8.f,\"%.2fEV\")"),
                std::string_view(
                    "DrawBoundedSliderFloat("
                    "\"DiffuseStrength##ImageBasedLighting\","
                    "&m_ui.DiffuseIblStrength,0.f,4.f,0.f,2.f,\"%.2f\")"),
                std::string_view(
                    "DrawBoundedSliderFloat("
                    "\"SpecularStrength##ImageBasedLighting\","
                    "&m_ui.SpecularIblStrength,0.f,4.f,0.f,2.f,\"%.2f\")"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"Sway\","
                    "&flashlight.swayDegrees,0.f,"
                    "FlashlightMaximumSwayDegrees,0.f,1.f,\"%.2fdegrees\")"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"BeamSize\","
                    "&flashlight.beamSizeDegrees,"
                    "FlashlightMinimumBeamSizeDegrees,"
                    "FlashlightMaximumBeamSizeDegrees,"
                    "FlashlightMinimumBeamSizeDegrees,60.f,\"%.1fdegrees\")"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"AngularSize\","
                    "&flashlight.angularSizeDegrees,"
                    "FlashlightMinimumAngularSizeDegrees,"
                    "FlashlightMaximumAngularSizeDegrees,"
                    "FlashlightMinimumAngularSizeDegrees,10.f,\"%.2fdegrees\")"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"AngularSize\","
                    "&light.angularSize,0.f,20.f,0.f,10.f)"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"InnerAngle\","
                    "&light.innerAngle,0.f,180.f,0.f,90.f)"),
                std::string_view(
                    "DrawBoundedSliderFloat(\"OuterAngle\","
                    "&light.outerAngle,0.f,180.f,0.f,90.f)") })
        {
            Require(
                CountOccurrences(compactSoftRangeSource, softRangeCall) == 1u,
                "every authored practical slider travel range must appear "
                "exactly once.");
        }
        Require(
            CountOccurrences(
                compactSoftRangeSource,
                "DrawBoundedSliderFloat(") == 18u &&
                CountOccurrences(
                    compactSoftRangeSource,
                    "DrawBoundedSliderInt(") == 4u,
            "the bounded-slider manifest must cover all 18 authored call "
            "sites in addition to the two wrapper declarations and two "
            "ordinary-wrapper forwards.");
        Require(
            CountOccurrences(viewer, "ImGui::SliderFloat(") == 1u &&
                CountOccurrences(viewer, "ImGui::SliderInt(") == 1u &&
                CountOccurrences(viewer, "ImGui::SliderScalar(") == 0u,
            "every UVSR slider call site must route through the logical/travel "
            "wrappers.");
        RequireAbsent(
            viewer,
            "app::AzimuthElevationSliders(",
            "Donut light-direction sliders that bypass visual-max policy");
        const std::string_view lightDirectionSliders = ExtractSection(
            viewer,
            "bool DrawLightDirectionSliders(",
            "static bool DrawCenteredActionButton(",
            "bounded directional-light angle sliders");
        RequireOrdered(
            lightDirectionSliders,
            {
                "auto [azimuth, elevation] = GetCommandLightAngles(",
                "bool changed = DrawSliderFloat(",
                "\"Azimuth\"",
                "&azimuth,",
                "-180.f,",
                "180.f,",
                "\"%.1f deg\"",
                "ImGuiSliderFlags_NoRoundToFormat);",
                "changed |= DrawSliderFloat(",
                "\"Elevation\"",
                "&elevation,",
                "-90.f,",
                "90.f,",
                "\"%.1f deg\"",
                "ImGuiSliderFlags_NoRoundToFormat);",
                "if (changed)",
                "direction = MakeCommandLightDirection("
            },
            "Azimuth and Elevation retain their exact angle domains and "
            "formatting while sharing Override Visual Maxes behavior");

        const std::string_view ratioSampleSlider = ExtractSection(
            viewer,
            "int sampleRateLog2 = multipleSamplesEnabled",
            "const NoiseSettings oldResolvedNoise =",
            "gated Heitz ratio-estimator sample slider");
        RequireOrdered(
            ratioSampleSlider,
            {
                "int sampleRateLog2 = multipleSamplesEnabled",
                "? ratio.sampleRateLog2",
                ": HeitzRatioEstimatorMinimumSampleRateLog2;",
                "int sampleRate = 1 << sampleRateLog2;",
                "BeginVisuallyDisabledUiScope(",
                "\"##RatioEstimatorSamplesDisabledPresentation\"",
                "!multipleSamplesEnabled);",
                "\"Samples Per Pixel##RatioEstimatorShadows\"",
                "&sampleRate,",
                "1 << HeitzRatioEstimatorMaximumSampleRateLog2,",
                "\"%d\"",
                "ImGuiSliderFlags_Logarithmic",
                "const int candidateSampleRateLog2 = std::clamp(",
                "std::lround(std::log2(",
                "std::max(1, sampleRate)",
                "if (candidateSampleRateLog2 != ratio.sampleRateLog2)",
                "ratio.sampleRateLog2 = candidateSampleRateLog2;",
                "m_app->ResetImageBasedLightingHistory();",
                "EndVisuallyDisabledUiScope(",
                "sampleSliderManualDisabledAlpha);"
            },
            "the Heitz sample slider exposes the real power-of-two sample count "
            "and maps accepted edits back to stored log2 configuration");
        const std::string_view skySampleSlider = ExtractSection(
            viewer,
            "int sampleRateLog2 = skyVisibility.useRatioEstimator",
            "if (DrawPresetResetIcon(\n"
            "                        \"RayTracedSkyVisibilitySamples\"",
            "gated sky-visibility sample slider");
        RequireOrdered(
            skySampleSlider,
            {
                "int sampleRateLog2 = skyVisibility.useRatioEstimator",
                "? skyVisibility.sampleRateLog2",
                ": RayTracedSkyVisibilityMinimumSampleRateLog2;",
                "int sampleRate = 1 << sampleRateLog2;",
                "const bool sampleSliderManualDisabledAlpha =",
                "BeginVisuallyDisabledUiScope(",
                "\"##SkyVisibilitySamplesDisabledPresentation\"",
                "!skyVisibility.useRatioEstimator);",
                "\"Samples Per Pixel##RayTracedSkyVisibility\"",
                "&sampleRate,",
                "1 << RayTracedSkyVisibilityMaximumSampleRateLog2,",
                "\"%d\"",
                "ImGuiSliderFlags_Logarithmic",
                "const int candidateSampleRateLog2 = std::clamp(",
                "std::lround(std::log2(",
                "std::max(1, sampleRate)",
                "if (candidateSampleRateLog2 !=",
                "skyVisibility.sampleRateLog2)",
                "skyVisibility.sampleRateLog2 =",
                "candidateSampleRateLog2;",
                "m_app->ResetImageBasedLightingHistory();",
                "EndVisuallyDisabledUiScope(",
                "sampleSliderManualDisabledAlpha);"
            },
            "the sky sample slider exposes the real power-of-two sample count, "
            "presents the effective minimum locally while gated, and maps edits "
            "back to stored log2 configuration");
        Require(
            CountOccurrences(viewer, "int sampleRate = 1 << sampleRateLog2;") ==
                    2u &&
                CountOccurrences(
                    viewer,
                    "static_cast<int>(std::lround(std::log2(") == 2u,
            "exactly the two power-of-two controls must edit visible sample "
            "counts and map them back to log2 storage.");
        Require(
            CountOccurrences(
                viewer,
                "BeginVisuallyDisabledUiScope(") == 3u,
            "the visual disabled helper has one definition and exactly the two "
            "effective-sample slider call sites");

        const std::string_view checkboxOverride = ExtractSection(
            imguiUiOverride,
            "bool ImGui::Checkbox(const char* label, bool* v)",
            "RenderFrame(bb.Min, bb.Max, GetColorU32(ImGuiCol_FrameBg)",
            "authored toggle presentation");
        RequireOrdered(
            checkboxOverride,
            {
                "if (g.UvsrStockWidgetRendering)",
                "bool completed_pending_value = false;",
                "if (pending_value >= 0)",
                "*v = resolved_value;",
                "completed_pending_value = true;",
                "stock_storage.SetInt(pending_value_key, -1);",
                "const bool is_visible = ItemAdd(total_bb, id);",
                "if (completed_pending_value)",
                "MarkItemEdited(id);",
                "if (!is_visible)",
                "return completed_pending_value;"
            },
            "Ogg flushes a pending authored-toggle value after ItemAdd and "
            "reports the completed change even when the stock item is clipped");
        RequireOrdered(
            checkboxOverride,
            {
                "if (g.UvsrStockWidgetRendering)",
                "const bool visual_target =",
                "const float target_position =",
                "if (!g.UvsrUiMotionEnabled)",
                "animated_position = target_position;",
                "else",
                "animated_position = ImLerp(",
                "if (pending_value >= 0 &&",
                "animated_position == target_position)",
                "*v = visual_target;",
                "pending_value = -1;"
            },
            "authored toggle motion snaps to the pending endpoint and commits "
            "in the same frame when animations are disabled, while Ogg exits "
            "through the stock branch");

        for (const std::string_view toggleContract : {
                std::string_view(
                    "void ImGui::SetUvsrUiAccentColors("),
                std::string_view(
                    "g.UvsrNegativeAccent = ImVec4("),
                std::string_view(
                    "ImSaturate(negative_accent.w)"),
                std::string_view(
                    "g.UvsrPositiveAccent = ImVec4("),
                std::string_view(
                    "ImSaturate(positive_accent.w)"),
                std::string_view("const float thumb_size ="),
                std::string_view("const ImVec4 thumb_color = ImLerp("),
                std::string_view("g.UvsrNegativeAccent,"),
                std::string_view("g.UvsrPositiveAccent,") })
        {
            RequireContains(
                imguiUiOverride,
                toggleContract,
                "dynamic RGBA semantic toggle-thumb contract");
        }
        RequireContains(
            skinApplication,
            "colors[ImGuiCol_SliderGrab] =\n"
            "                tokens.drawerHeader;\n"
            "            colors[ImGuiCol_SliderGrabActive] =\n"
            "                tokens.drawerHeaderActive;",
            "authored slider grabs use Primary Accent rest and active tokens");
        RequireOrdered(
            skinApplication,
            {
                "ImGui::SetUvsrSliderTrackColors(",
                "tokens.drawerFrame,",
                "tokens.drawerFrameHovered,",
                "tokens.drawerFrameActive);"
            },
            "authored slider tracks use the ordinary drawer-frame states while "
            "Primary Accent remains isolated to the grab");
        RequireOrdered(
            imguiSliderOverride,
            {
                "static ImU32 GetUvsrSliderTrackColor(",
                "bool active)",
                "const ImGuiCol stock_color = active",
                "if (g.UvsrStockWidgetRendering)",
                "return ImGui::GetColorU32(stock_color);",
                "g.UvsrSliderTrackActive",
                "g.UvsrSliderTrackHovered"
            },
            "Ogg retains stock slider tracks while authored sliders consume "
            "the supplied drawer-frame states");
        const std::string_view baseSliderTrackColor = ExtractSection(
            imguiUiOverride,
            "+static ImU32 GetUvsrSliderTrackColor(",
            "// Note: p_data, p_min and p_max are _pointers_",
            "base authored slider-track color routing");
        RequireOrdered(
            baseSliderTrackColor,
            {
                "g.UvsrSliderTrackActive",
                "g.UvsrSliderTrackHovered",
                ": g.UvsrSliderTrack);"
            },
            "authored slider track active, hover, and resting color routing");
        RequireAbsent(
            imguiSliderOverride,
            "-                : g.UvsrSliderTrack);",
            "slider override preservation of the authored resting track color");
        RequireOrdered(
            imguiSliderOverride,
            {
                "static float GetUvsrAuthoredToggleTrackWidth()",
                "return IM_TRUNC(ImGui::GetFrameHeight() * 1.72f);",
                "const float toggle_width = GetUvsrAuthoredToggleTrackWidth();",
                "const float value_width =",
                "GetUvsrAuthoredToggleTrackWidth() * 2.0f;"
            },
            "the authored slider value bubble is exactly twice the toggle width");
        for (const std::string_view sliderContract : {
                std::string_view("const float cross_axis_size ="),
                std::string_view("grab_sz = ImMax(1.0f, cross_axis_size);"),
                std::string_view(
                    "slider_bb.Expand(ImVec2(0.0f, -2.0f));") })
        {
            RequireContains(
                imguiSliderOverride,
                sliderContract,
                "uniform non-overlapping slider thumb contract");
        }
        const std::string_view sliderGrabAnimation = ExtractSection(
            imguiUiOverride,
            "+static void AnimateSliderGrab(",
            "// Note: p_data, p_min and p_max are _pointers_",
            "authored slider grab animation");
        RequireOrdered(
            sliderGrabAnimation,
            {
                "ImHashStr(",
                "##SliderGrabAnimation",
                "id);",
                "##SliderGrabAnimationFrame",
                "id);",
                "!g.UvsrUiMotionEnabled",
                "g.ActiveId == id",
                "last_animation_frame < g.FrameCount - 1",
                "else if (last_animation_frame != g.FrameCount)",
                "const float delta_time = ImClamp(",
                "1.0f / 30.0f",
                "const float animation_half_life = 0.040f;",
                "1.0f - ImPow(",
                "animated_position = ImLerp(",
                "storage.SetFloat(animation_key, animated_position);",
                "storage.SetInt(animation_frame_key, g.FrameCount);"
            },
            "per-ID, frame-rate-independent 40 ms slider-grab motion with "
            "active and skipped-submission snapping");
        Require(
            CountOccurrences(imguiUiOverride, "AnimateSliderGrab(") == 3u &&
                CountOccurrences(
                    imguiUiOverride,
                    "GetRaisedFrameGradientBottomColor(grab_col)") == 2u,
            "horizontal and vertical authored sliders must animate then render "
            "their grabs with raised gradient depth.");
        RequireOrdered(
            imguiUiOverride,
            {
                "AnimateSliderGrab(",
                "ImGuiAxis_X,",
                "RenderGradientFrame(",
                "GetRaisedFrameGradientBottomColor(grab_col)",
                "style.GrabRounding,",
                "true);"
            },
            "horizontal slider grab animation precedes raised rendering");
        const std::string_view horizontalSliderGrab = ExtractSection(
            imguiUiOverride,
            "bool ImGui::SliderScalar(",
            "// Display value using user-provided display format",
            "authored-versus-stock horizontal slider grab");
        RequireOrdered(
            horizontalSliderGrab,
            {
                "if (!g.UvsrStockWidgetRendering)",
                "AnimateSliderGrab(",
                "ImGuiAxis_X,",
                "if (g.UvsrStockWidgetRendering)",
                "window->DrawList->AddRectFilled(",
                "grab_col,",
                "style.GrabRounding);",
                "else",
                "RenderGradientFrame(",
                "GetRaisedFrameGradientBottomColor(grab_col)",
                "true);"
            },
            "Ogg uses the exact upstream grab primitive while authored "
            "horizontal sliders alone animate and render raised depth");
        const std::string_view verticalSliderGrab = ExtractSection(
            imguiUiOverride,
            "const bool value_changed = SliderBehavior(frame_bb, id, data_type, "
                "p_data, p_min, p_max, format, flags | "
                "ImGuiSliderFlags_Vertical, &grab_bb);",
            "// Display value using user-provided display format",
            "authored-versus-stock vertical slider grab");
        RequireOrdered(
            verticalSliderGrab,
            {
                "if (!g.UvsrStockWidgetRendering)",
                "AnimateSliderGrab(",
                "ImGuiAxis_Y,",
                "if (g.UvsrStockWidgetRendering)",
                "window->DrawList->AddRectFilled(",
                "grab_col,",
                "style.GrabRounding);",
                "else",
                "RenderGradientFrame(",
                "GetRaisedFrameGradientBottomColor(grab_col)",
                "true);"
            },
            "Ogg uses the exact upstream grab primitive while authored "
            "vertical sliders alone animate and render raised depth");

        RequireAbsent(
            imguiSliderOverride,
            "UvsrSliderValueLabelPresentation",
            "retired moving slider-label presentation state");
        for (const std::string_view retiredSliderLabelState : {
                std::string_view("##SliderValueLabelSide"),
                std::string_view("##SliderValueLabelOpacity"),
                std::string_view("##SliderValueLabelFrame") })
        {
            RequireAbsent(
                imguiSliderOverride,
                retiredSliderLabelState,
                "retired per-ID slider-label animation state");
        }
        const std::string_view sliderValueLane = ExtractSection(
            imguiSliderOverride,
            "+struct UvsrSliderValueLane",
            "static ImU32 GetUvsrSliderTrackColor(",
            "authored fixed slider value lane");
        RequireOrdered(
            sliderValueLane,
            {
                "bool Usable;",
                "ImRect TrackRect;",
                "ImRect ValueRect;",
                "static UvsrSliderValueLane ResolveUvsrSliderValueLane(",
                "const float frame_height)",
                "const float value_width =",
                "GetUvsrAuthoredToggleTrackWidth() * 2.0f;",
                "constexpr float BubbleGap = 2.0f;",
                "slider_bb.GetWidth() - value_width - BubbleGap;",
                "if (track_width < frame_height)",
                "return { false, slider_bb, ImRect() };",
                "value_rect.Min.x = value_rect.Max.x - value_width;",
                "track_rect.Max.x = value_rect.Min.x - BubbleGap;",
                "return { true, track_rect, value_rect };"
            },
            "authored sliders split their existing frame into a left track and "
            "double-toggle-width right value bubble with an exact two-pixel gap "
            "and a safe narrow-width fallback without increasing total width");
        const std::string_view sliderNumericFormatting = ExtractSection(
            imguiSliderOverride,
            "+static const char* GetUvsrSliderExactInputFormat(",
            "static ImU32 GetUvsrSliderTrackColor(",
            "authored slider display and exact-input formatting");
        RequireOrdered(
            sliderNumericFormatting,
            {
                "GetUvsrSliderExactInputFormat(",
                "if (data_type == ImGuiDataType_Float)",
                "return \"%.9g\";",
                "if (data_type == ImGuiDataType_Double)",
                "return \"%.17g\";",
                "return ImGui::DataTypeGetInfo(data_type)->PrintFmt;",
                "static int CountUvsrNumericDigits(const char* text)",
                "if (*cursor >= '0' && *cursor <= '9')",
                "static int FormatUvsrFourDigitSliderValue(",
                "if (!(value >= -DBL_MAX && value <= DBL_MAX))",
                "ImFormatString(output, output_size, \"----\")",
                "value = 0.0; // Normalize negative zero.",
                "const int fixed_precision = ImMax(0, 4 - integer_digits);",
                "for (int precision = fixed_precision;",
                "precision >= 0;",
                "--precision)",
                "\"%.*f\"",
                "precision,",
                "if (CountUvsrNumericDigits(fixed) == 4)",
                "ImFormatString(output, output_size, \"%s\", fixed)",
                "ImFormatString(output, output_size, \"----\")"
            },
            "authored display text contains exactly four numeric glyphs, "
            "excluding sign and decimal, or a letter-free overflow marker; "
            "exact input retains native float, double, and integer precision");
        const std::string_view authoredSliderFormatting = ExtractSection(
            imguiSliderOverride,
            "+static int FormatUvsrFourDigitSliderValue(",
            "static ImU32 GetUvsrSliderTrackColor(",
            "authored fixed-decimal slider formatter");
        Require(
            CountOccurrences(
                authoredSliderFormatting,
                "ImFormatString(") == 4u,
            "the authored formatter must have only the nonfinite fallback, fixed "
            "decimal candidate, accepted candidate, and overflow fallback output "
            "paths; it must never introduce scientific notation or letters.");
        Require(
            CountOccurrences(
                imguiSliderOverride,
                "GetUvsrSliderExactInputFormat(data_type)") == 2u &&
                CountOccurrences(
                    imguiSliderOverride,
                    "FormatUvsrFourDigitSliderValue(") == 2u,
            "the exact-input formatter must serve both authored temp-input paths, "
            "while the four-digit formatter serves only authored display text.");
        RequireOrdered(
            imguiSliderOverride,
            {
                "g.UvsrStockWidgetRendering",
                "? DataTypeFormatString(",
                ": FormatUvsrFourDigitSliderValue(",
                "if (g.UvsrStockWidgetRendering)",
                "RenderTextClipped(frame_bb.Min, frame_bb.Max",
                "else if (value_lane.Usable)",
                "value_lane.ValueRect.Min +"
            },
            "Ogg keeps caller-formatted centered text while authored sliders use "
            "the four-digit formatter in the fixed right value bubble");
        RequireOrdered(
            imguiSliderOverride,
            {
                "const bool hide_prefix = !g.UvsrStockWidgetRendering ||",
                "IM_TRUNC(w_items / components)",
                "CalcTextSize((flags & ImGuiColorEditFlags_Float)"
            },
            "authored ColorEdit component fields always omit R/G/B/A prefixes "
            "while Ogg retains the stock width-sensitive prefix rule");
        RequireOrdered(
            imguiSliderOverride,
            {
                "ImRect slider_bb = frame_bb;",
                "slider_bb.Expand(ImVec2(0.0f, -2.0f));",
                "const UvsrSliderValueLane value_lane =",
                "ResolveUvsrSliderValueLane(",
                "GetFrameHeight());",
                "const bool value_lane_hovered =",
                "IsMouseHoveringRect(",
                "value_lane.ValueRect.Min,",
                "value_lane.ValueRect.Max);",
                "const bool track_hovered =",
                "value_lane.TrackRect.Min,",
                "value_lane.TrackRect.Max);",
                "const bool direct_value_input =",
                "value_lane_hovered &&",
                "const bool make_active =",
                "direct_value_input ||",
                "ctrl_value_input ||",
                "(clicked && track_hovered) ||",
                "const ImVec2 cursor_pos_backup = window->DC.CursorPos;",
                "const ImVec2 cursor_pos_prev_line_backup = window->DC.CursorPosPrevLine;",
                "const ImVec2 cursor_max_pos_backup = window->DC.CursorMaxPos;",
                "const ImVec2 curr_line_size_backup = window->DC.CurrLineSize;",
                "const ImVec2 prev_line_size_backup = window->DC.PrevLineSize;",
                "const float curr_line_text_base_offset_backup =",
                "const float prev_line_text_base_offset_backup =",
                "const bool is_same_line_backup = window->DC.IsSameLine;",
                "const bool is_set_pos_backup = window->DC.IsSetPos;",
                "const bool value_changed = TempInputScalar(",
                "value_lane.ValueRect,",
                "window->DC.CursorPos = cursor_pos_backup;",
                "window->DC.CursorPosPrevLine = cursor_pos_prev_line_backup;",
                "window->DC.CursorMaxPos = cursor_max_pos_backup;",
                "window->DC.CurrLineSize = curr_line_size_backup;",
                "window->DC.PrevLineSize = prev_line_size_backup;",
                "window->DC.CurrLineTextBaseOffset =",
                "window->DC.PrevLineTextBaseOffset =",
                "window->DC.IsSameLine = is_same_line_backup;",
                "window->DC.IsSetPos = is_set_pos_backup;",
                "return value_changed;",
                "if (value_lane.Usable)",
                "RenderFrame(",
                "value_lane.TrackRect.Min,",
                "value_lane.TrackRect.Max,",
                "g.Style.FrameRounding);",
                "RenderFrame(",
                "value_lane.ValueRect.Min,",
                "value_lane.ValueRect.Max,",
                "g.Style.FrameRounding);",
                "else",
                "RenderFrame(",
                "slider_bb.Min,",
                "slider_bb.Max,",
                "SliderBehavior(value_lane.TrackRect,",
                "if (g.UvsrStockWidgetRendering)",
                "RenderTextClipped(frame_bb.Min, frame_bb.Max",
                "else if (value_lane.Usable)",
                "value_lane.ValueRect.Min +",
                "ImVec2(1.0f, 0.5f)"
            },
            "authored value-lane clicks use only the compact visible lane, restore "
            "the full slider's row layout after direct scalar input, render two "
            "fully rounded faces or one safe fallback, drive only the left track, "
            "and leave Ogg's centered stock text untouched");
        RequireAbsent(
            imguiSliderOverride,
            "AddLine(",
            "retired separator between the independently rounded slider faces");
        RequireAbsent(
            imguiSliderOverride,
            "ImVec2(value_lane.ValueRect.Min.x, frame_bb.Min.y)",
            "retired full-height slider value-lane hover rectangle");
        RequireOrdered(
            cmakeSource,
            {
                "overrides/donut-app.patch",
                "overrides/donut-loading-app.patch",
                "overrides/donut-app-ui-polish.patch",
                "overrides/imgui-ui.patch",
                "overrides/imgui-slider-controls.patch",
                "overrides/imgui-combo-roll.patch",
                "overrides/imgui-ui-polish.patch",
                "overrides/imgui-tooltip-picker.patch"
            },
            "Donut callback polish and ImGui tooltip/picker polish compose "
            "after their prerequisite overrides");
        for (const std::string_view retiredPatch : {
                std::string_view("imgui-dropdown-roll.patch"),
                std::string_view("imgui-runtime-policy.patch") })
        {
            RequireAbsent(
                cmakeSource,
                retiredPatch,
                "retired dedicated ImGui override patch");
        }

        RequireOrdered(
            imguiUiOverride,
            {
                "const bool uvsr_compact_popup = !g.UvsrStockWidgetRendering;",
                "ImGuiStyleVar_ItemSpacing,",
                "original_item_spacing_y * 0.25f",
                "ImGuiStyleVar_WindowPadding,",
                "g.Style.PopupRounding + original_item_spacing_y * 0.5f",
                "BeginUvsrComboPopupHighlight(GetCurrentWindow(), g);"
            },
            "authored combo popups use compact scoped spacing and one shared "
            "highlight layer before the later roll patch adds lifecycle state");
        const std::string_view popupHighlight = ExtractSection(
            imguiUiOverride,
            "+static ImGuiID GetUvsrComboPopupHighlightKey(",
            "bool ImGui::BeginCombo(const char* label",
            "shared authored combo-popup highlight");
        RequireOrdered(
            popupHighlight,
            {
                "static void BeginUvsrComboPopupHighlight(",
                "TargetPriority",
                "window->DrawList->ChannelsSplit(2);",
                "window->DrawList->ChannelsSetCurrent(1);",
                "static void RegisterUvsrComboPopupHighlight(",
                "priority <= storage.GetInt(priority_key, 0)",
                "rect.Min - window->Pos",
                "rect.Max - window->Pos",
                "static void EndUvsrComboPopupHighlight(",
                "const bool submission_was_interrupted =",
                "!g.UvsrUiMotionEnabled",
                "const float delta_time = ImClamp(",
                "1.0f / 30.0f",
                "const float half_life = 0.055f;",
                "current_rect.Min = ImLerp(",
                "current_rect.Max = ImLerp(",
                "current_color = ImLerp(",
                "window->DrawList->ChannelsSetCurrent(0);",
                "window->DrawList->AddRectFilled(",
                "current_rect.Min + window->Pos",
                "g.Style.FrameRounding);",
                "window->DrawList->ChannelsMerge();"
            },
            "one popup-scoped rounded fill glides between registered rows with "
            "frame-rate-independent motion and a skipped-frame/master-switch snap");
        const std::string_view comboSelectable = ExtractSection(
            imguiUiOverride,
            "+        const bool uvsr_combo_popup =",
            "if (g.NavId == id)",
            "combo-popup Selectable registration");
        RequireOrdered(
            comboSelectable,
            {
                "const bool uvsr_combo_popup =",
                "!g.UvsrStockWidgetRendering",
                "strncmp(window->Name, \"##Combo_\", 8) == 0;",
                "const int priority = highlighted ? 2 : selected ? 1 : 0;",
                "RegisterUvsrComboPopupHighlight(",
                "held && highlighted",
                "? ImGuiCol_HeaderActive",
                "? ImGuiCol_HeaderHovered",
                ": ImGuiCol_Header],",
                "priority);",
                "else if (highlighted || selected)",
                "RenderFrame(bb.Min, bb.Max, col, false, 0.0f);"
            },
            "authored popup Selectables only register one shared target while "
            "Ogg and non-combo Selectables retain stock rendering");
        RequireAbsent(
            comboSelectable,
            "GetAnimatedHighlightAmount(",
            "retired per-Selectable combo highlight animation");
        const std::string_view highlightAnimation = ExtractSection(
            imguiUiOverride,
            "+static float GetAnimatedHighlightAmount(",
            "+static ImVec4 LerpWidgetColor(",
            "authored interaction highlight animation");
        RequireOrdered(
            highlightAnimation,
            {
                "if (!g.UvsrUiMotionEnabled)",
                "return highlighted ? 1.0f : 0.0f;",
                "##HighlightFadeAmount",
                "##HighlightFadeFrame",
                "amount = ImLerp(",
                "ImSaturate(g.IO.DeltaTime * speed)",
                "storage.SetFloat(amount_key, amount);"
            },
            "authored closed controls retain per-ID smooth hover motion and "
            "snap immediately under the animation master switch");

        RequireOrdered(
            imguiComboRollOverride,
            {
                "IMGUI_API bool          IsComboPopupTransitionActive(ImGuiID combo_id);",
                "IMGUI_API void          FinishComboPopupTransition(ImGuiID combo_id);",
                "static constexpr float UvsrComboPopupRollDuration = 0.18f;",
                "static constexpr float UvsrComboPopupMaximumDelta = 1.0f / 30.0f;",
                "static float AdvanceComboPopupRollElapsed(",
                "UvsrComboPopupMaximumDelta",
                "static float SmoothComboPopupRoll(",
                "progress * progress * (3.0f - 2.0f * progress);",
                "static void ApplyComboPopupRollClip("
            },
            "the public exact-combo API and frame-rate-bounded 180 ms smooth "
            "roll implementation are installed after the slider patch");
        const std::string compactComboRoll = Compact(imguiComboRollOverride);
        RequireContains(
            compactComboRoll,
            "UvsrComboPopupMaximumDelta));",
            "combo roll DeltaTime cap");
        RequireOrdered(
            imguiComboRollOverride,
            {
                "static void ApplyComboPopupRollClip(ImGuiWindow* window)",
                "##ComboPopupRollAmount",
                "##ComboPopupRollFromBottom",
                "for (int command_index = 0;",
                "command.ClipRect.y = ImMax(",
                "command.ClipRect.w = ImMin(",
                "if (command.ClipRect.w < command.ClipRect.y)",
                "command.ClipRect.w = command.ClipRect.y;"
            },
            "roll clipping preserves fixed popup layout and clamps every merged "
            "draw command to a valid reveal boundary");
        RequireOrdered(
            imguiComboRollOverride,
            {
                "bool ImGui::IsComboPopupTransitionActive(ImGuiID combo_id)",
                "ImHashStr(\"##ComboPopup\", 0, combo_id);",
                "popup_data.PopupId != popup_id",
                "##ComboPopupClosing",
                "##ComboPopupInteractionReady",
                "return true;",
                "void ImGui::FinishComboPopupTransition(ImGuiID combo_id)",
                "g.OpenPopupStack[popup_index].PopupId != popup_id",
                "ClosePopupToLevel(popup_index, true);"
            },
            "the transition query and forced finish address only the exact "
            "originating combo popup");
        Require(
            CountOccurrences(
                imguiComboRollOverride,
                "FinishComboPopupTransition(id);") == 2u,
            "BeginCombo must finish an exact retained popup on both SkipItems "
            "and clipped ItemAdd early returns.");
        RequireOrdered(
            imguiComboRollOverride,
            {
                "const bool same_popup_lifecycle =",
                "popup_open_frame_key,",
                "popup_data.OpenFrameCount;",
                "if (popup_closing && !uvsr_roll_enabled)",
                "ClosePopupToLevel(g.BeginPopupStack.Size, true);",
                "popup_close_ready_frame >= 0 &&",
                "g.FrameCount > popup_close_ready_frame",
                "Preserve one fully rolled-up endpoint frame",
                "popup_roll_amount =",
                "1.0f - SmoothComboPopupRoll(popup_close_elapsed);",
                "popup_interaction_ready = false;",
                "next_popup_close_ready_frame = g.FrameCount;",
                "popup_roll_amount =",
                "SmoothComboPopupRoll(popup_open_elapsed);",
                "popup_open_elapsed >= UvsrComboPopupRollDuration;",
                "Latch the reveal",
                "##ComboPopupRollFromBottom",
                "popup_storage.SetInt(popup_closing_key, 0);"
            },
            "combo windows key retained roll state by popup lifecycle, present "
            "one hidden close endpoint, and latch their reveal edge");
        RequireOrdered(
            imguiComboRollOverride,
            {
                "const bool popup_closing = storage.GetInt(",
                "##ComboPopupClosing",
                "##UvsrComboHighlightTargetFrame",
                "if (!popup_closing)",
                "##UvsrComboHighlightTargetPriority",
                "0);"
            },
            "the clicked priority-two shared highlight target remains latched "
            "while the popup rolls closed");
        RequireOrdered(
            imguiComboRollOverride,
            {
                "EndUvsrComboPopupHighlight(window, g);",
                "ApplyComboPopupRollClip(window);",
                "EndPopup();",
                "if (uvsr_compact_popup)",
                "PopStyleVar(2);"
            },
            "shared highlight channels merge before all popup commands are "
            "roll-clipped and the native popup/style lifecycle ends");
        RequireOrdered(
            imguiComboRollOverride,
            {
                "const bool uvsr_combo_popup =",
                "IsUvsrComboPopupWindow(window);",
                "const bool uvsr_combo_roll_enabled =",
                "uvsr_combo_popup && g.UvsrUiMotionEnabled;",
                "const bool combo_popup_interaction_blocked =",
                "##ComboPopupClosing",
                "##ComboPopupInteractionReady",
                "if (!combo_popup_interaction_blocked)",
                "pressed = ButtonBehavior(",
                "else if (g.ActiveId == id)",
                "ClearActiveID();",
                "if (!combo_popup_interaction_blocked && (flags & ImGuiSelectableFlags_SelectOnNav)",
                "if (uvsr_combo_roll_enabled)",
                "##ComboPopupClosing",
                "##ComboPopupCloseElapsed",
                "##ComboPopupCloseReadyFrame",
                "##ComboPopupInteractionReady",
                "else",
                "CloseCurrentPopup();"
            },
            "opening and closing rolls discard mouse/nav input without replay, "
            "while Ogg and motion-disabled authored popups close immediately");
        for (const std::string_view retiredPopupContract : {
                std::string_view("ComboPopupOpenAnimation"),
                std::string_view("ComboPopupPendingSelectable"),
                std::string_view("GetPopupSelectionHoverColor"),
                std::string_view("GetSelectionGradientBottomColor"),
                std::string_view("UiComboPopupRoll"),
                std::string_view("DeferredUiStructuralPresentation"),
                std::string_view("AwaitPopupRollUp") })
        {
            RequireAbsent(
                imguiUiOverride,
                retiredPopupContract,
                "retired popup-roll predecessor state in the base UI override");
            RequireAbsent(
                imguiComboRollOverride,
                retiredPopupContract,
                "retired popup-roll replay or renderer-structural state");
            RequireAbsent(
                viewer,
                retiredPopupContract,
                "retired popup-roll application state");
            RequireAbsent(
                uiAnimation,
                retiredPopupContract,
                "retired opened-popup animation helper");
        }
        const std::string_view colorPickerScope = ExtractSection(
            viewer,
            "constexpr float SettingsWindowWidthInFontHeights = 23.44f;",
            "ImDrawList* settingsBodyDrawList =",
            "Settings color-picker lane and scope");
        RequireOrdered(
            colorPickerScope,
            {
                "constexpr float SettingsWindowWidthInFontHeights = 23.44f;",
                "const float availableWindowWidth =",
                "settingsPanelMarginPixels * 2.f",
                "const float colorPickerMinimumSelectorWidth =",
                "ImGui::GetFrameHeight() * 4.f;",
                "const float colorPickerPopupHorizontalPadding =",
                "style.WindowPadding.x + style.ItemInnerSpacing.x;",
                "const float colorPickerMinimumLaneWidth =",
                "(colorPickerMinimumSelectorWidth * 4.f +",
                "style.ItemInnerSpacing.x) / 3.f) +",
                "colorPickerPopupHorizontalPadding * 2.f;",
                "const float settingsWindowMaximumWidth =",
                "availableWindowWidth -",
                "colorPickerMinimumLaneWidth);",
                "const float settingsWindowMinimumWidth =",
                "settingsControlWidth +",
                "style.WindowPadding.x * 4.f +",
                "style.ScrollbarSize;",
                "const float settingsWindowWidth = std::min(",
                "fontSize * SettingsWindowWidthInFontHeights,",
                "settingsWindowMinimumWidth),",
                "settingsWindowMaximumWidth);",
                "ImGui::BeginChild(",
                "\"##SettingsBody\"",
                "const bool settingsScrolledThisFrame =",
                "previousScrollContext.lastFrame ==",
                "ImGui::GetFrameCount() - 1",
                "settingsBodyWindow->Scroll.y -",
                "previousScrollContext.lastScrollY",
                "if (settingsScrolledThisFrame)",
                "ImGui::CloseUvsrColorPickerPopup();",
                "const float colorPickerMaximumBottom = std::min(",
                "panelStackMaximumBottom,",
                "settingsBodyWindow->ParentWindow->Pos.y +",
                "settingsBodyWindow->ParentWindow->Size.y);",
                "ImGui::PushUvsrColorPickerPopupContentRight(",
                "settingsBodyWindow->InnerRect.Max.x,",
                "colorPickerMaximumBottom,",
                "g_UiVisualTokens.colorPickerSurface,",
                "g_UiVisualTokens.panelInsetFrame,",
                "colorPickerContentLayer,",
                "colorPickerControlLayer);"
            },
            "the 20-percent-narrower Settings width keeps its text-safe content "
            "floor, padded viewport/picker-lane cap, scoped bottom bound, "
            "translucent popup surface, separate opaque rim, and two depth layers");
        RequireAbsent(
            viewer,
            "SettingsWindowWidthInFontHeights = 29.3f",
            "retired pre-reduction Settings width");
        RequireOrdered(
            viewer,
            {
                "ImGui::PushUvsrColorPickerPopupContentRight(",
                "DrawInterfaceDrawer(settingsControlWidth);",
                "ImGui::PopUvsrColorPickerPopupContentRight();",
                "ImGui::EndChild();"
            },
            "the picker placement scope encloses all Settings controls and "
            "balances before ending the child");
        RequireOrdered(
            viewer,
            {
                "TrackSettingsAppearanceDrawList(\n"
                "            ImGui::GetUvsrActiveColorPickerPopupDrawList());",
                "for (ImDrawList* drawList :\n"
                "            g_SettingsAppearanceDrawLists)",
                "ApplyWindowAppearance("
            },
            "the active current-frame picker draw list joins the shared menu "
            "appearance transform exactly once through deduplicated tracking");
        const std::string_view settingsAppearanceTracking = ExtractSection(
            viewer,
            "static void TrackAppearanceDrawList(",
            "static bool IsSettingsChildLaterInDrawOrder(",
            "panel appearance draw-list deduplication");
        RequireOrdered(
            settingsAppearanceTracking,
            {
                "if (drawList &&",
                "std::find(",
                "drawLists.begin(),",
                "drawLists.end(),",
                "drawList) == drawLists.end())",
                "drawLists.push_back(drawList);",
                "static void TrackSettingsAppearanceDrawList(",
                "g_SettingsAppearanceDrawLists,",
                "static void TrackPerformanceAppearanceDrawList(",
                "g_PerformanceAppearanceDrawLists,"
            },
            "one draw list can receive each owning panel's appearance transform "
            "at most once per frame");
        for (const std::string_view pickerApi : {
                std::string_view(
                    "PushUvsrColorPickerPopupContentRight(float content_right, "
                    "float maximum_bottom, const ImVec4& popup_background);"),
                std::string_view(
                    "PopUvsrColorPickerPopupContentRight();"),
                std::string_view(
                    "ImDrawList*   GetUvsrActiveColorPickerPopupDrawList();"),
                std::string_view(
                    "CloseUvsrColorPickerPopup();") })
        {
            RequireContains(
                imguiUiOverride,
                pickerApi,
                "public scoped color-picker popup placement API");
        }
        RequireOrdered(
            imguiUiOverride,
            {
                "struct ImGuiUvsrColorPickerPopupScope",
                "float                   ContentRight;",
                "float                   MaximumBottom;",
                "ImVec4                  PopupBackground;",
                "ImVector<ImGuiUvsrColorPickerPopupScope> "
                    "UvsrColorPickerPopupScopeStack;",
                "ImGuiID                 UvsrActiveColorPickerPopupId = 0;",
                "ImDrawList*             "
                    "UvsrActiveColorPickerPopupDrawList = NULL;",
                "int                     "
                    "UvsrActiveColorPickerPopupDrawListFrame = -1;"
            },
            "picker scope owns its horizontal and vertical limits plus surface, "
            "while exact popup identity and current-frame draw-list state remain "
            "separate from generic popups");
        const std::string_view colorPickerOverride = ExtractSection(
            imguiUiOverride,
            "void ImGui::PushUvsrColorPickerPopupContentRight(",
            "// Edit colors components",
            "scoped color-picker popup placement override");
        RequireOrdered(
            colorPickerOverride,
            {
                "const ImGuiUvsrColorPickerPopupScope scope = {",
                "content_right,",
                "maximum_bottom,",
                "popup_background",
                "g.UvsrColorPickerPopupScopeStack.push_back(scope);",
                "void ImGui::PopUvsrColorPickerPopupContentRight()",
                "g.UvsrColorPickerPopupScopeStack.pop_back();",
                "ImDrawList* ImGui::GetUvsrActiveColorPickerPopupDrawList()",
                "UvsrActiveColorPickerPopupDrawListFrame !=",
                "context->FrameCount",
                "return context->UvsrActiveColorPickerPopupDrawList;",
                "void ImGui::CloseUvsrColorPickerPopup()",
                "const ImGuiID popup_id =",
                "context->UvsrActiveColorPickerPopupId;",
                "context->UvsrActiveColorPickerPopupId = 0;",
                "context->OpenPopupStack[popup_index].PopupId != popup_id",
                "ClosePopupToLevel(popup_index, true);",
                "static UvsrColorPickerPopupLayout ConfigureUvsrColorPickerPopup(",
                "ImGui::GetPopupAllowedExtentRect(parent_window)",
                "allowed.Max.y = ImMin(allowed.Max.y, maximum_bottom);",
                "const float minimum_picker_width =",
                "const float minimum_outer_width =",
                "const float minimum_outer_height =",
                "const float requested_x = ImClamp(",
                "content_right,",
                "const float available_width =",
                "allowed.Max.x - requested_x",
                "const float available_height =",
                "allowed.Max.y - allowed.Min.y",
                "available_width >= minimum_outer_width &&",
                "available_height >= minimum_outer_height",
                "if (!layout.HasUsableLane)",
                "if (layout.HideSidePreview)",
                "outer_width = ImMin(outer_width, available_width);",
                "const ImVec2 outer_size(",
                "ImMin(expected_outer_height, available_height));",
                "const ImVec2 popup_position(",
                "requested_x,",
                "ImClamp(",
                "default_anchor.y,",
                "allowed.Min.y,",
                "ImMax(allowed.Min.y, allowed.Max.y - outer_size.y)",
                "const float maximum_outer_height =",
                "ImGui::SetNextWindowSizeConstraints(",
                "ImVec2(outer_size.x, maximum_outer_height)",
                "ImGui::SetNextWindowPos(popup_position, ImGuiCond_Always);"
            },
            "picker popup begins at content-right, follows its source before "
            "clamping at the caller-owned menu-stack bottom, fails closed "
            "without a usable two-axis lane, reports only the current frame's "
            "draw list, and can close only its exact popup ID");
        const std::string_view scopedColorPickerIntegration = ExtractSection(
            imguiUiOverride,
            "const ImVec2 picker_anchor =",
            "EndPopup();",
            "scoped ColorEdit4 picker popup");
        RequireOrdered(
            scopedColorPickerIntegration,
            {
                "g.UvsrColorPickerPopupScopeStack.Size > 0;",
                "ConfigureUvsrColorPickerPopup(",
                "scoped_picker_scope->ContentRight,",
                "scoped_picker_scope->MaximumBottom,",
                "(!scoped_picker_popup || picker_layout.HasUsableLane)",
                "!picker_layout.HasUsableLane &&",
                "IsPopupOpen(\"picker\")",
                "ClosePopupToLevel(g.BeginPopupStack.Size, true);",
                "const bool position_picker_popup =",
                "PushStyleVar(ImGuiStyleVar_WindowMinSize",
                "if (scoped_picker_popup)",
                "PushStyleColor(",
                "ImGuiCol_PopupBg,",
                "scoped_picker_scope->PopupBackground);",
                "const bool picker_popup_open = BeginPopup(\"picker\");",
                "PopStyleColor();",
                "g.UvsrActiveColorPickerPopupId =",
                "g.CurrentWindow->PopupId;",
                "g.UvsrActiveColorPickerPopupDrawList =",
                "g.CurrentWindow->DrawList;",
                "g.UvsrActiveColorPickerPopupDrawListFrame =",
                "g.FrameCount;",
                "picker_flags_to_forward",
                "ImGuiColorEditFlags_AlphaBar",
                "SetNextItemWidth(picker_layout.PickerWidth);",
                "ColorPicker4(\"##picker\", col, picker_flags"
            },
            "ColorEdit4 forwards alpha-bar interaction through the scoped, "
            "viewport-safe popup and applies the caller-owned surface only "
            "around that picker BeginPopup");
        Require(
            CountOccurrences(
                scopedColorPickerIntegration,
                "scoped_picker_scope->PopupBackground") == 1u,
            "the scoped surface color must affect exactly the picker popup, "
            "without leaking into generic combo, context, or option popups.");
        const std::string_view roundedPickerHelpers = ExtractSection(
            imguiUiOverride,
            "struct UvsrVerticalGradientStop",
            "static void RenderArrowsForVerticalBar",
            "rounded authored color-picker primitives");
        RequireOrdered(
            roundedPickerHelpers,
            {
                "static void AddUvsrRoundedVerticalGradient(",
                "rounding = ImClamp(rounding, 0.0f, "
                    "ImMin(width, height) * 0.5f);",
                "const float fringe = "
                    "(draw_list->Flags & ImDrawListFlags_AntiAliasedFill)",
                "GetUvsrRoundedRectDistance(bounds, rounding, position);",
                "GetUvsrVerticalGradientColor(stops, stops_count, "
                    "gradient_position, coverage)",
                "static void AddUvsrRoundedTriangle(",
                "draw_list->PathBezierQuadraticCurveTo(",
                "static void RenderUvsrRoundedMarkersForVerticalBar(",
                "const ImVec2 left_outer_tip(",
                "const ImVec2 right_outer_tip(",
                "AddUvsrRoundedTriangle(",
                "AddUvsrRoundedTriangle(",
                "AddUvsrRoundedTriangle(",
                "AddUvsrRoundedTriangle("
            },
            "authored picker gradients retain antialiased rounded coverage and "
            "each vertical bar receives paired rounded black-and-white markers");
        const std::string_view wheelPickerRendering = ExtractSection(
            imguiUiOverride,
            "const bool render_uvsr_wheel_with_bars =",
            "// Render alpha bar",
            "authored wheel picker and retained hue bar");
        RequireOrdered(
            imguiTooltipPickerAdded,
            {
                "static void RenderUvsrHueWheelEdgeOutlines(",
                "const float fringe = ImMax(1.0f, draw_list->_FringeScale);",
                "const float radii[2] = { inner_radius, outer_radius };",
                "draw_list->AddCircle(",
                "const float outline_alpha = ImLerp(",
                "0.95f,",
                "0.55f,",
                "vertex.col = ImGui::GetColorU32(ImVec4(",
                "outline_alpha * coverage));",
                "RenderUvsrHueWheelEdgeOutlines(",
                "wheel_r_inner,",
                "wheel_r_outer);"
            },
            "the authored hue wheel adds visible one-pixel white transparency "
            "gradients to both edges before drawing cursors");
        RequireOrdered(
            wheelPickerRendering,
            {
                "g.UvsrColorPickerPopupScopeStack.Size > 0 &&",
                "!g.UvsrStockWidgetRendering &&",
                "(flags & ImGuiColorEditFlags_PickerHueWheel) != 0;",
                "const float uvsr_bar_rounding = "
                    "ImMin(style.FrameRounding, bars_width * 0.25f);",
                "if (flags & ImGuiColorEditFlags_PickerHueWheel)",
                "if (render_uvsr_wheel_with_bars)",
                "UvsrVerticalGradientStop hue_stops[7];",
                "hue_stops[hue_index].Position = "
                    "(float)hue_index / 6.0f;",
                "const ImRect hue_bar_bounds(",
                "AddUvsrRoundedVerticalGradient(",
                "IM_ARRAYSIZE(hue_stops),",
                "uvsr_bar_rounding);",
                "uvsr_hue_bar_line_y =",
                "RenderFrameBorder(",
                "hue_bar_bounds.Max,",
                "uvsr_bar_rounding);"
            },
            "only an authored scoped hue-wheel picker adds a retained, rounded "
            "seven-stop hue bar while Ogg and unscoped pickers remain upstream");
        const std::string_view alphaBarRendering = ExtractSection(
            imguiUiOverride,
            "// Render alpha bar",
            "if (render_uvsr_picker_cursors_last)",
            "rounded authored alpha bar and stock fallback");
        RequireOrdered(
            alphaBarRendering,
            {
                "if (alpha_bar)",
                "if (render_uvsr_wheel_with_bars)",
                "RenderColorRectWithAlphaCheckerboard(",
                "uvsr_bar_rounding);",
                "const UvsrVerticalGradientStop alpha_stops[2]",
                "AddUvsrRoundedVerticalGradient(",
                "IM_ARRAYSIZE(alpha_stops),",
                "uvsr_bar_rounding);",
                "uvsr_alpha_bar_line_y =",
                "RenderFrameBorder(bar1_bb.Min, bar1_bb.Max, "
                    "uvsr_bar_rounding);",
                "else",
                "RenderColorRectWithAlphaCheckerboard(draw_list, "
                    "bar1_bb.Min, bar1_bb.Max, 0,",
                "draw_list->AddRectFilledMultiColor(",
                "RenderFrameBorder(bar1_bb.Min, bar1_bb.Max, 0.0f);",
                "RenderArrowsForVerticalBar("
            },
            "authored alpha preserves the checkerboard under a rounded gradient "
            "and rounded border while the Ogg/unscoped path remains stock");
        const std::string_view colorPickerCursorOverride = ExtractSection(
            imguiUiOverride,
            "const bool render_uvsr_picker_cursors_last =",
            "EndGroup();",
            "authored color-picker cursor layering");
        RequireOrdered(
            colorPickerCursorOverride,
            {
                "g.UvsrColorPickerPopupScopeStack.Size > 0 &&",
                "!g.UvsrStockWidgetRendering;",
                "if (!render_uvsr_picker_cursors_last)",
                "if (alpha_bar)",
                "if (render_uvsr_picker_cursors_last)",
                "draw_list->PushClipRect(",
                "window->OuterRectClipped.Min,",
                "window->OuterRectClipped.Max,",
                "if (render_uvsr_wheel_with_bars)",
                "RenderUvsrRoundedMarkersForVerticalBar(",
                "ImVec2(bar0_pos_x - 1.0f, uvsr_hue_bar_line_y)",
                "if (alpha_bar)",
                "RenderUvsrRoundedMarkersForVerticalBar(",
                "ImVec2(bar1_pos_x - 1.0f, uvsr_alpha_bar_line_y)",
                "draw_list->AddCircleFilled(",
                "draw_list->AddCircle(",
                "cursor_white",
                "draw_list->PopClipRect();"
            },
            "authored scoped picker rounded bar markers precede the final wheel "
            "and SV cursors inside the popup outer clip, while Ogg and unscoped "
            "pickers retain the upstream marker and cursor order");

        RequireOrdered(
            imguiUiPolishOverride,
            {
                "PushUvsrColorPickerPopupContentRight(float content_right, "
                    "float maximum_bottom, const ImVec4& popup_background, "
                    "const ImVec4& content_layer, const ImVec4& picker_layer);",
                "BeginUvsrTreeArrowCapture();",
                "EndUvsrTreeArrowCapture(float open_amount, "
                    "bool upside_down = false);"
            },
            "the final ordered override exposes scoped picker depth colors and "
            "one-arrow drawer rotation capture");
        const std::string_view treeArrowPolish = ExtractSection(
            imguiUiPolishOverride,
            "void ImGui::BeginUvsrTreeArrowCapture()",
            "void ImGui::RenderBullet(",
            "scoped drawer-arrow rotation override");
        RequireOrdered(
            treeArrowPolish,
            {
                "g.UvsrTreeArrowCapture = ImGuiUvsrTreeArrowCapture{};",
                "g.UvsrTreeArrowCapture.Armed = true;",
                "g.UvsrTreeArrowCapture.Frame = g.FrameCount;",
                "void ImGui::EndUvsrTreeArrowCapture(",
                "capture.Frame != g.FrameCount",
                "capture.SourceDirection == ImGuiDir_Down",
                "ImSaturate(open_amount)",
                "upside_down ? -IM_PI * 0.5f : IM_PI * 0.5f",
                "capture.DrawList->VtxBuffer[vertex_index]",
                "const int vertex_start = draw_list->VtxBuffer.Size;",
                "capture.Armed = false;",
                "capture.SourceDirection = dir;",
                "capture_rendered_arrow();"
            },
            "only the armed frame's next rendered arrow is captured, then its "
            "authored vertices rotate continuously from upright to sideways");

        const std::string_view pickerDepthPolish = ExtractSection(
            imguiUiPolishOverride,
            "struct ImGuiUvsrColorPickerPopupScope",
            "static ImU32 GetUvsrSvSelectorColor(",
            "scoped color-picker depth layers");
        RequireOrdered(
            pickerDepthPolish,
            {
                "ImVec4                  ContentLayer;",
                "ImVec4                  PickerLayer;",
                "const ImVec4& content_layer,",
                "const ImVec4& picker_layer)",
                "popup_background,",
                "content_layer,",
                "picker_layer",
                "static ImRect GetUvsrColorPickerPopupContentBounds(",
                "style.WindowPadding.x",
                "style.WindowPadding.y",
                "window->InnerRect.Min + inset,",
                "window->InnerRect.Max - inset",
                "static void DrawUvsrColorPickerPopupContentLayer(",
                "window->DrawList->PushClipRect(",
                "bounds.Min,",
                "bounds.Max,",
                "DrawUvsrColorPickerPopupContentLayer(",
                "g.UvsrStockWidgetRendering ? NULL : g.CurrentWindow,",
                "scoped_picker_scope->ContentLayer"
            },
            "the popup margin remains visible around an inset translucent "
            "content layer on the same popup draw list");
        RequireOrdered(
            imguiUiPolishOverride,
            {
                "const ImRect picker_content_bounds =",
                "GetUvsrColorPickerPopupContentBounds(window);",
                "const ImVec2 layer_offset(",
                "ImRect picker_layer_bounds(",
                "picker_pos - layer_offset,",
                "picker_pos.y + sv_picker_size",
                "picker_layer_bounds.ClipWith(picker_content_bounds);",
                "draw_list->PushClipRect(",
                "picker_content_bounds.Min,",
                "picker_content_bounds.Max,",
                "ImGui::GetColorU32(picker_scope.PickerLayer)"
            },
            "the second translucent picker layer stays inside the full-padding "
            "content layer and ends above the bottom component controls");

        const std::string_view roundedSelectorPolish = ExtractSection(
            imguiUiPolishOverride,
            "struct UvsrRoundedTriangleSelector",
            "static void AddUvsrRoundedCheckerboard(",
            "full-gamut rounded SV triangle");
        RequireOrdered(
            roundedSelectorPolish,
            {
                "ImVec2 SharpVertices[3];",
                "ImVec2 Center;",
                "ImVector<ImVec2> Contour;",
                "float EndpointSnapDistance = 1.0f;",
                "static float GetUvsrRayPolygonBoundaryDistance(",
                "static void BuildUvsrRoundedTriangleSelector(",
                "ImMax(rounding, 0.0f) * 1.75f",
                "minimum_edge_length * 0.20f",
                "draw_list->_CalcCircleAutoSegmentCount(",
                "selector.Contour.push_back(",
                "entry * (inverse_amount * inverse_amount)",
                "static ImVec2 MapUvsrSharpPointToRoundedTriangle(",
                "rounded_boundary / sharp_boundary",
                "static ImVec2 MapUvsrSvToRoundedTriangle(",
                "selector.SharpVertices[2]",
                "selector.SharpVertices[0]",
                "selector.SharpVertices[1]",
                "static bool MapUvsrRoundedTriangleToSv(",
                "const ImVec2 endpoint_positions[3]",
                "*saturation = 1.0f;",
                "*value = 1.0f;",
                "*saturation = 0.0f;",
                "*value = 0.0f;",
                "ImTriangleBarycentricCoords(",
                "*value = ImSaturate(1.0f - black_weight);",
                "? ImSaturate(hue_weight / *value)",
                "static void AddUvsrRoundedTriangleSelector(",
                "const float maximum_mesh_step = ImMax(",
                "1.5f,",
                "const int sections = ImClamp(",
                "24,",
                "192);",
                "const int vertex_count = (sections + 1) * (sections + 2) / 2;",
                "const int index_count = sections * sections * 3;",
                "draw_list->PrimReserve(index_count, vertex_count);"
            },
            "a radial sharp-to-rounded triangle bijection retains exact hue, "
            "black, and white endpoints while fine bounded tessellation avoids "
            "coarse selector corners");
        for (const std::string_view retiredSquareSelector : {
                std::string_view("GetUvsrRoundedRectHorizontalSpan"),
                std::string_view("MapUvsrSvToRoundedSelector"),
                std::string_view("MapUvsrRoundedSelectorToSv"),
                std::string_view("AddUvsrRoundedSvSelector"),
                std::string_view("uvsr_selector_bounds"),
                std::string_view("uvsr_selector_half_extent") })
        {
            RequireAbsent(
                imguiUiPolishOverride,
                retiredSquareSelector,
                "retired rounded-square selector implementation");
        }
        RequireOrdered(
            imguiUiPolishOverride,
            {
                "UvsrRoundedTriangleSelector uvsr_selector;",
                "if (render_uvsr_wheel_with_bars)",
                "BuildUvsrRoundedTriangleSelector(",
                "triangle_pa,",
                "triangle_pb,",
                "triangle_pc,",
                "style.FrameRounding,",
                "ImMax(style.FrameRounding, draw_list->_FringeScale));",
                "MapUvsrRoundedTriangleToSv(",
                "const float color_control_alpha =",
                "render_uvsr_wheel_with_bars ? 1.0f : style.Alpha;",
                "AddUvsrRoundedTriangleSelector(",
                "MapUvsrSvToRoundedTriangle(",
                "TransformUvsrRoundedTrianglePoint(",
                "else",
                "Render the stock SV triangle rotated according to hue."
            },
            "authored scoped pickers allocate, interact with, and render the "
            "rounded full-gamut triangle while stock and unscoped pickers "
            "retain the upstream triangle");
        RequireOrdered(
            imguiUiPolishOverride,
            {
                "const float color_control_alpha =",
                "render_uvsr_wheel_with_bars ? 1.0f : style.Alpha;",
                "IM_F32_TO_INT8_SAT(color_control_alpha)",
                "ImVec4 hue_color_f(1, 1, 1, color_control_alpha)",
                "ImVec4(R, G, B, color_control_alpha)",
                "AddUvsrRoundedTriangleSelector(",
                "color_control_alpha,",
                "AddUvsrRoundedCheckerboard(",
                "color_control_alpha);",
                "RenderUvsrRoundedMarkersForVerticalBar("
            },
            "translucent authored popup layers do not attenuate the steady-state "
            "selector, bars, checker, previews, or markers");

        const std::string_view synchronousTooltipPolish = ExtractSection(
            imguiTooltipPickerAdded,
            "static ImGuiID GetUvsrTooltipOwnerId()",
            "struct ImGuiUvsrColorPickerPopupTransition",
            "caller-scoped tooltip animation override");
        const std::string_view canonicalTooltipSubmission = ExtractSection(
            imguiTooltipPickerAdded,
            "static ImGuiWindow* SubmitUvsrAuthoredTooltipTextV(",
            "static void SubmitUvsrItemTooltipV(",
            "canonical authored tooltip submission");
        RequireContains(
            imguiTooltipPickerAdded,
            "IMGUI_API void          SetUvsrAuthoredWindowPadding(",
            "public canonical authored-window padding API");
        RequireContains(
            imguiTooltipPickerAdded,
            "ImVec2                  UvsrAuthoredWindowPadding;",
            "context-owned canonical authored-window padding");
        RequireContains(
            imguiTooltipPickerAdded,
            "UvsrAuthoredWindowPadding = Style.WindowPadding;",
            "default canonical authored-window padding");
        RequireOrdered(
            synchronousTooltipPolish,
            {
                "static void ResetUvsrTooltipAnimation(",
                "state = ImGuiUvsrTooltipState();",
                "static void UpdateUvsrTooltipAppearance(",
                "if (!g.UvsrUiMotionEnabled)",
                "state.FrameStartAppearance = state.Appearance;",
                "state.LastAdvancedFrame = g.FrameCount;",
                "constexpr float duration = 0.18f;",
                "state.FrameStartAppearance + step",
                "state.FrameStartAppearance - step",
                "static void ApplyUvsrTooltipAppearance(",
                "const ImVec2 pivot = tooltip_window->Pos;",
                "vertex.pos = pivot + (vertex.pos - pivot) * scale;",
                "(float)alpha * eased",
                "static void FormatUvsrBoundedTooltipTextV(",
                "ImFormatStringToTempBufferV(&text, &text_end, fmt, args);",
                "constexpr int maximum_code_points = 120;",
                "constexpr int retained_code_points =",
                "maximum_code_points - ellipsis_code_points;",
                "output.append(\"...\");",
                "static ImGuiWindow* SubmitUvsrAuthoredTooltipTextV(",
                "FormatUvsrBoundedTooltipTextV(text, fmt, args);",
                "constexpr float tooltip_inner_margin = 5.0f;",
                "const ImVec2 tooltip_window_padding =",
                "g.UvsrAuthoredWindowPadding;",
                "g.FontSize * 20.0f,",
                "ImGui::GetMainViewport()->WorkSize.x * 0.42f",
                "g.FontSize * 7.0f,",
                "ImGui::GetMainViewport()->WorkSize.y * 0.25f",
                "tooltip_window_padding.x * 2.0f",
                "ImGui::SetNextWindowSize(tooltip_size, ImGuiCond_Always);",
                "ImGui::PushStyleVar(",
                "ImGuiStyleVar_WindowPadding,",
                "tooltip_window_padding);",
                "ImGui::BeginTooltipEx(",
                "ImGui::PopStyleVar();",
                "ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(",
                "ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);",
                "ImGui::TextUnformatted(text.begin(), text.end());",
                "ImGui::EndTooltip();",
                "ImGui::PopStyleVar();",
                "static void SubmitUvsrItemTooltipV(",
                "if (g.DragDropActive)",
                "const ImGuiID owner_id = GetUvsrTooltipOwnerId();",
                "ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);",
                "if (!hovered && state.OwnerId != owner_id)",
                "state.LastOwnerSeenFrame = g.FrameCount;",
                "UpdateUvsrTooltipAppearance(state, hovered);",
                "SubmitUvsrAuthoredTooltipTextV(fmt, args);",
                "ApplyUvsrTooltipAppearance(tooltip_window, state.Appearance);",
                "state.LastSubmittedWindow = tooltip_window;",
                "state.LastSubmittedFrame = g.FrameCount;",
                "static void FinalizeUvsrTooltipAnimationFrame()",
                "g.UvsrStockWidgetRendering ||",
                "g.DragDropActive ||",
                "state.LastOwnerSeenFrame != g.FrameCount",
                "state.LastSubmittedFrame == g.FrameCount &&",
                "g.TooltipPreviousWindow == state.LastSubmittedWindow",
                "ImGui::SetWindowHiddenAndSkipItemsForCurrentFrame(",
                "ResetUvsrTooltipAnimation(state);"
            },
            "authored item tooltips cap Unicode text at 120 code points, share "
            "one fixed outer size, inset and wrap policy, and reversibly animate");
        RequireAbsent(
            canonicalTooltipSubmission,
            "g.Style.WindowPadding",
            "nested-layout-independent authored tooltip padding");
        RequireOrdered(
            imguiTooltipPickerOverride,
            {
                "if (GImGui->UvsrStockWidgetRendering)",
                "if (IsItemHovered(ImGuiHoveredFlags_ForTooltip))",
                "SetTooltipV(fmt, args);",
                "else",
                "SubmitUvsrItemTooltipV(fmt, args);",
                "void ImGui::SetItemTooltipV(",
                "SubmitUvsrItemTooltipV(fmt, args);"
            },
            "both variadic item-tooltip entry points isolate authored synchronous "
            "animation while stock policy keeps the ordinary SetTooltipV path");
        RequireOrdered(
            imguiTooltipPickerOverride,
            {
                "void ImGui::SetTooltipV(const char* fmt, va_list args)",
                "if (!GImGui->UvsrStockWidgetRendering)",
                "SubmitUvsrAuthoredTooltipTextV(fmt, args);",
                "ImGuiTextBuffer text;",
                "FormatUvsrBoundedTooltipTextV(text, fmt, args);",
                "TextUnformatted(text.begin(), text.end());"
            },
            "stock and authored tooltip submission share the same 120-code-point cap");
        Require(
            CountOccurrences(
                imguiTooltipPickerAdded,
                "FinalizeUvsrTooltipAnimationFrame();") == 2u &&
                CountOccurrences(
                    imguiTooltipPickerAdded,
                    "SubmitUvsrItemTooltipV") == 3u,
            "The authored tooltip must have one EndFrame cleanup hook and two "
            "caller-scoped SetItemTooltip submission routes.");
        RequireOrdered(
            imguiTooltipPickerAdded,
            {
                "struct ImGuiUvsrTooltipState",
                "ImGuiID                 OwnerId = 0;",
                "float                   Appearance = 0.0f;",
                "float                   FrameStartAppearance = 0.0f;",
                "int                     LastOwnerSeenFrame = -1;",
                "int                     LastAdvancedFrame = -1;",
                "ImGuiWindow*            LastSubmittedWindow = NULL;",
                "int                     LastSubmittedFrame = -1;",
                "ImGuiUvsrTooltipState   UvsrTooltipState;"
            },
            "one context-owned tooltip keeps only reversible appearance, owner, "
            "stale-frame, and exact late-submission identity state");
        for (const std::string_view retiredTooltipContract : {
                std::string_view("##Tooltip_UvsrInteractive"),
                std::string_view("InteractiveSubmission"),
                std::string_view("QueueUvsrDirectTooltipV"),
                std::string_view("QueueUvsrItemTooltipV"),
                std::string_view("NoWindowHoverableCheck") })
        {
            RequireAbsent(
                imguiTooltipPickerAdded,
                retiredTooltipContract,
                "retired interactive authored tooltip branch");
        }
        const std::string_view stockTooltipCore = ExtractSection(
            imguiUpstream,
            "bool ImGui::BeginTooltipEx(",
            "void ImGui::EndTooltip()",
            "stock tooltip window contract");
        RequireOrdered(
            stockTooltipCore,
            {
                "const bool is_dragdrop_tooltip =",
                "const char* window_name_template =",
                "ImGuiWindowFlags flags = ImGuiWindowFlags_Tooltip | "
                    "ImGuiWindowFlags_NoInputs",
                "ImGuiWindowFlags_AlwaysAutoResize;",
                "Begin(window_name, NULL, flags | extra_window_flags);"
            },
            "the unchanged stock tooltip window remains non-interactive and "
            "content auto-sized");
        const std::string_view stockTooltipPlacement = ExtractSection(
            imguiUpstream,
            "// Position tooltip (always follows mouse + clamp within outer boundaries)",
            "IM_ASSERT(0);",
            "stock pointer-relative tooltip placement");
        RequireOrdered(
            stockTooltipPlacement,
            {
                "Position tooltip (always follows mouse + clamp within outer boundaries)",
                "const ImVec2 ref_pos = NavCalcPreferredRefPos();",
                "TOOLTIP_DEFAULT_OFFSET_MOUSE * scale",
                "FindBestWindowPosForPopupEx(",
                "ImGuiPopupPositionPolicy_Tooltip"
            },
            "authored synchronous tooltips inherit upstream pointer-relative, "
            "viewport-clamped placement");

        const std::string_view finalPickerLayout = ExtractSection(
            imguiTooltipPickerOverride,
            "bool AuthoredBarLayout;",
            "static void AddUvsrRoundedPointerTriangle(",
            "final uniform four-bar picker layout");
        RequireOrdered(
            finalPickerLayout,
            {
                "bool AuthoredBarLayout;",
                "int BarCount;",
                "struct UvsrAuthoredColorPickerGeometry",
                "float SelectorSize;",
                "float BarWidth;",
                "float FirstBarOffset;",
                "GetUvsrAuthoredPickerWidthForSelector(",
                "(selector_size * 4.0f + inner_spacing) / 3.0f",
                "ResolveUvsrAuthoredColorPickerGeometry(",
                "picker_width - inner_spacing * 3.0f,",
                "IM_TRUNC(items_width * 0.75f) + inner_spacing * 3.0f,",
                "(fourth_column_width - inner_spacing * 3.0f) / 4.0f,",
                "ImMax(fourth_column_offset - inner_spacing, 1.0f),",
                "float source_center_y,",
                "const ImVec2& popup_padding,",
                "int authored_bar_count,",
                "const bool authored_bar_layout = authored_bar_count == 4;",
                "const float normal_selector_size =",
                "square_sz * 10.0f - style.ItemInnerSpacing.x * 2.0f;",
                "GetUvsrAuthoredPickerWidthForSelector(",
                "const float minimum_selector_size = square_sz * 4.0f;",
                "3.0f * (square_sz + style.ItemSpacing.y)",
                "if (!authored_bar_layout && has_visible_label)",
                "const float sv_picker_height = authored_bar_layout",
                "ResolveUvsrAuthoredColorPickerGeometry(",
                "const float requested_y = source_center_y - outer_size.y * 0.5f;",
                "ImClamp(",
                "requested_y,",
                "allowed.Min.y,",
                "ImMax(allowed.Min.y, allowed.Max.y - outer_size.y)",
                "if (authored_bar_layout)",
                "ImGui::SetNextWindowSize(outer_size, ImGuiCond_Always);",
                "ImGui::SetNextWindowPos(popup_position, ImGuiCond_Always);"
            },
            "the authored picker derives four equal lanes from the fourth input "
            "column, suppresses visible-label height, uses captured popup padding, "
            "and centers on its source until an allowed edge clamps it");
        RequireAbsent(
            finalPickerLayout,
            "const float requested_y = authored_bar_layout",
            "retired authored bottom-only picker placement");

        const std::string_view pickerTransitionState = ExtractSection(
            imguiTooltipPickerAdded,
            "struct ImGuiUvsrColorPickerPopupTransition",
            "struct ImGuiUvsrTooltipState",
            "retained picker transition state");
        RequireOrdered(
            pickerTransitionState,
            {
                "ImGuiID                 PopupId = 0;",
                "int                     OpenFrame = -1;",
                "float                   Amount = 0.0f;",
                "float                   FrameStartAmount = 0.0f;",
                "bool                    TargetOpen = false;",
                "int                     ZeroFrame = -1;",
                "int                     LastAdvancedFrame = -1;",
                "int                     LastSubmittedFrame = -1;",
                "bool                    RestoreFocus = false;",
                "int                     ReopenFrame = -1;",
                "ImRect                  SourceRect;"
            },
            "one exact picker identity retains reversible amount, close endpoint, "
            "focus, submission, reopen, and source-pointer state");
        const std::string_view pickerTransitionHelpers = ExtractSection(
            imguiTooltipPickerAdded,
            "static void ResetUvsrColorPickerPopupTransition(",
            "bool AuthoredBarLayout;",
            "retained picker transition helpers");
        RequireOrdered(
            pickerTransitionHelpers,
            {
                "BeginUvsrColorPickerPopupTransition(",
                "transition.PopupId != popup_id || transition.OpenFrame != open_frame",
                "transition.TargetOpen = true;",
                "transition.Amount = g.UvsrUiMotionEnabled ? 0.0f : 1.0f;",
                "transition.SourceRect = source_rect;",
                "AdvanceUvsrColorPickerPopupTransition(",
                "transition.FrameStartAmount = transition.Amount;",
                "transition.FrameStartAmount + step",
                "transition.FrameStartAmount - step",
                "transition.ZeroFrame = g.FrameCount;",
                "ApplyUvsrColorPickerPopupTransition(",
                "vertex.pos = pivot + (vertex.pos - pivot) * scale;",
                "command.ClipRect = ImVec4("
            },
            "picker fade/zoom updates its source through retained close, "
            "reverses from a once-per-frame baseline, and transforms both "
            "geometry and clip rectangles");
        RequireAbsent(
            pickerTransitionHelpers,
            "if (transition.TargetOpen)",
            "stale closing-picker source latch");
        RequireOrdered(
            imguiTooltipPickerOverride,
            {
                "picker_transition.PopupId == id &&",
                "!picker_transition.TargetOpen &&",
                "picker_transition.Amount > 0.0f &&",
                "picker_transition.TargetOpen = true;",
                "picker_transition.ReopenFrame = g.FrameCount;",
                "static void ClosePopupToLevelImmediately(",
                "transition.PopupId != 0 &&",
                "remaining + 1 == g.OpenPopupStack.Size",
                "transition.TargetOpen = false;",
                "transition.RestoreFocus |= restore_focus_to_window_under_popup;",
                "void ImGui::CommitUvsrColorPickerPopupClose()",
                "ClosePopupToLevelImmediately(popup_index, transition.RestoreFocus);",
                "transition = ImGuiUvsrColorPickerPopupTransition();",
                "const int authored_bar_count = authored_bar_popup ? 4 : 0;",
                "const bool reversing_same_picker =",
                "if (!reversing_same_picker)",
                "g.ColorPickerRef = col_v4;",
                "BeginUvsrColorPickerPopupTransition(",
                "g.FrameCount > g.UvsrColorPickerPopupTransition.ZeroFrame",
                "const bool picker_interaction_ready =",
                "g.UvsrColorPickerPopupTransition.Amount >= 1.0f",
                "EndPopup();",
                "AdvanceUvsrColorPickerPopupTransition(g);",
                "ApplyUvsrColorPickerPopupTransition("
            },
            "the exact authored picker can reverse a retained close without "
            "resetting its reference color, defers physical close through a "
            "visible zero frame, blocks transitional input, and transforms only "
            "after popup composition");

        const std::string_view finalPickerPointer = ExtractSection(
            imguiTooltipPickerAdded,
            "static void DrawUvsrColorPickerSourcePointer(",
            "float EndpointSnapRadius = 1.0f;",
            "rounded picker source pointer and popup integration");
        RequireOrdered(
            finalPickerPointer,
            {
                "const float tip_x = parent_window->WorkRect.Max.x +",
                "const float base_x = popup_window->Pos.x + (fringe + 1.0f) * scale;",
                "source_rect.GetCenter().y,",
                "popup_top + style.PopupRounding + unscaled_arrow_half + fringe,",
                "popup_bottom - style.PopupRounding - unscaled_arrow_half - fringe",
                "ImGui::GetPopupAllowedExtentRect(parent_window)",
                "allowed.Max.y = ImMin(allowed.Max.y, scope.MaximumBottom);",
                "draw_list->PushClipRect(allowed.Min, allowed.Max, false);",
                "AddUvsrRoundedPointerTriangle(",
                "ImGui::GetColorU32(scope.OuterMarginLayer)",
                "ImGui::GetColorU32(scope.ContentLayer)",
                "const bool color_button_pressed =",
                "ColorButton(\"##ColorButton\", col_v4, flags);",
                "const ImRect picker_source_rect = g.LastItemData.Rect;",
                "picker_source_rect.GetCenter().y,",
                "const ImRect transition_source_rect =",
                "transition_source_rect.GetCenter().y,",
                "if (picker_layout.AuthoredBarLayout)",
                "PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);",
                "if (picker_layout.HideSidePreview ||",
                "picker_layout.AuthoredBarLayout)",
                "DrawUvsrColorPickerSourcePointer(",
                "transition_source_rect,"
            },
            "the authored popup captures its source row, suppresses the stock "
            "side preview and border, and draws a rounded viewport-clipped "
            "pointer to the canonical Settings content edge");
        RequireAbsent(
            finalPickerPointer,
            "const float tip_x = source_rect.Max.x +",
            "retired inset-sensitive pointer target");
        RequireAbsent(
            finalPickerPointer,
            "transition_picker_anchor",
            "retired below-swatch popup anchor");
        RequireContains(
            imguiTooltipPickerOverride,
            "picker_flags |= ImGuiColorEditFlags_NoSidePreview;",
            "authored four-bar picker suppresses the stock side preview");
        RequireOrdered(
            imguiTooltipPickerOverride,
            {
                "ImVec4                  PopupSurface;",
                "ImVec4                  OuterMarginLayer;",
                "ImVec4                  ContentLayer;",
                "ImVec4                  PickerLayer;",
                "ImVec2                  FramePadding;",
                "ImVec2                  PopupPadding;",
                "const ImVec4& popup_surface,",
                "const ImVec4& outer_margin_layer,",
                "g.UvsrAuthoredWindowPadding,",
                "g.UvsrAuthoredWindowPadding + g.Style.ItemInnerSpacing",
                "static void DrawUvsrColorPickerInsetFrame(",
                "const ImU32 frame_color = ImGui::GetColorU32(color);",
                "draw_list->AddRectFilled(",
                "const auto draw_inner_corner_wedge =",
                "draw_list->AddConcavePolyFilled(",
                "vertex.col = ImGui::GetUvsrCarvedFrameOutlineColor(",
                "static ImRect GetUvsrColorPickerPopupContentBounds(",
                "scope.FramePadding.x",
                "scope.FramePadding.y",
                "static void DrawUvsrColorPickerPopupContentLayer(",
                "DrawUvsrColorPickerInsetFrame(",
                "ImGui::GetColorU32(scope.ContentLayer)",
                "ImGui::GetColorU32(scope.OuterMarginLayer)",
                "ImGui::GetColorU32(scope.ContentLayer)",
                "scoped_picker_scope->PopupSurface"
            },
            "the picker separates Regular frame padding from the Tight control "
            "inset, fills the complete Settings-matched frame band, and keeps "
            "the base and interior layers on their existing opacity paths");
        RequireAbsent(
            finalPickerPointer,
            "ImGui::ColorConvertFloat4ToU32(scope.OuterMarginLayer)",
            "caller-alpha-independent picker frame path");
        const std::string_view finalPickerPopupStyle = ExtractSection(
            imguiTooltipPickerOverride,
            "const bool submit_picker_popup =",
            "if (picker_popup_open)",
            "authored picker popup style submission");
        RequireOrdered(
            finalPickerPopupStyle,
            {
                "if (submit_picker_popup)",
                "PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));",
                "if (picker_layout.AuthoredBarLayout)",
                "PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);",
                "ImGuiStyleVar_WindowPadding,",
                "scoped_picker_scope->PopupPadding);",
                "scoped_picker_scope->PopupSurface",
                "const bool picker_popup_open = BeginPopup(\"picker\");",
                "if (submit_picker_popup)",
                "PopStyleVar(picker_layout.AuthoredBarLayout ? 3 : 1);"
            },
            "the real popup begin path applies captured canonical padding and "
            "balances all authored style variables");
        const std::string_view finalPickerControlLayer = ExtractSection(
            imguiTooltipPickerAdded,
            "static void DrawUvsrColorPickerPopupControlLayer(",
            "static void AddUvsrRoundedPointerTriangle(",
            "final authored picker control layer");
        RequireOrdered(
            finalPickerControlLayer,
            {
                "GetUvsrColorPickerPopupContentBounds(window, scope);",
                "content_bounds.Min,",
                "content_bounds.Max,",
                "ImGui::ColorConvertFloat4ToU32(scope.PickerLayer)"
            },
            "the brightest picker layer fills the complete inner surface while "
            "retaining its supplied alpha independently of caller alpha");
        RequireAbsent(
            finalPickerControlLayer,
            "ImGui::GetColorU32(scope.PickerLayer)",
            "caller-alpha-dependent picker control layer");
        const std::string_view finalPickerContentSubmission = ExtractSection(
            imguiTooltipPickerOverride,
            "const bool picker_interaction_ready =",
            "ImGuiWindow* submitted_picker_window =",
            "authored picker content submission");
        RequireOrdered(
            finalPickerContentSubmission,
            {
                "DrawUvsrColorPickerPopupControlLayer(",
                "*scoped_picker_scope);",
                "value_changed |= ColorPicker4(\"##picker\""
            },
            "the authored control depth layer is submitted before its picker assets");
        RequireAbsent(
            finalPickerPointer,
            "ImGui::GetColorU32(ImGuiCol_Border)",
            "retired opaque stock border on the authored picker pointer");

        const std::string_view finalSelectorSnap = ExtractSection(
            imguiTooltipPickerAdded,
            "float EndpointSnapRadius = 1.0f;",
            "static void RenderUvsrHollowMarkerForVerticalBar(",
            "final rounded-selector snap affordances");
        RequireOrdered(
            finalSelectorSnap,
            {
                "float EndpointSnapRadius = 1.0f;",
                "float EndpointMarkerRadius = 1.0f;",
                "float BoundaryHitSlop = 1.0f;",
                "float endpoint_snap_radius,",
                "float endpoint_marker_radius,",
                "float boundary_hit_slop)",
                "selector.EndpointSnapRadius = ImMax(1.0f, endpoint_snap_radius);",
                "selector.EndpointMarkerRadius = ImMax(1.0f, endpoint_marker_radius);",
                "selector.BoundaryHitSlop = ImMax(1.0f, boundary_hit_slop);",
                "selector.EndpointSnapRadius * selector.EndpointSnapRadius",
                "selector.BoundaryHitSlop",
                "static void RenderUvsrRoundedTriangleSnapPoints(",
                "const float radius = selector.EndpointMarkerRadius;",
                "for (const ImVec2& sharp_vertex : selector.SharpVertices)",
                "MapUvsrSharpPointToRoundedTriangle(selector, sharp_vertex)",
                "draw_list->AddCircle(",
                "IM_COL32(24, 24, 24, alpha8)",
                "draw_list->AddCircle(",
                "IM_COL32(255, 255, 255, alpha8)"
            },
            "endpoint hit, visible marker, and ordinary rounded-boundary slop "
            "use separate radii while retaining three hollow gamut targets");
        RequireAbsent(
            finalSelectorSnap,
            "AddCircleFilled(",
            "rounded selector snap points remain hollow");
        const std::string_view finalVerticalMarker = ExtractSection(
            imguiTooltipPickerAdded,
            "static void RenderUvsrHollowMarkerForVerticalBar(",
            "const ImU32 comparison_colors[2] =",
            "final vertical bar marker and interaction routing");
        RequireOrdered(
            finalVerticalMarker,
            {
                "float radius,",
                "radius = ImMax(fringe, radius);",
                "const ImVec2 center(bar_bounds.GetCenter().x, marker_y);",
                "draw_list->AddCircle(",
                "IM_COL32(24, 24, 24, alpha8)",
                "draw_list->AddCircle(",
                "IM_COL32(255, 255, 255, alpha8)",
                "ref_col != NULL &&",
                "flags |= ImGuiColorEditFlags_NoSidePreview;",
                "ResolveUvsrAuthoredColorPickerGeometry(",
                "sv_picker_size = authored_geometry.SelectorSize;",
                "bars_width = authored_geometry.BarWidth;",
                "picker_pos.x + authored_geometry.FirstBarOffset",
                "float bar2_pos_x =",
                "float bar3_pos_x =",
                "? bar2_pos_x",
                "? bar3_pos_x",
                "const float compact_marker_radius =",
                "compact_marker_radius + draw_list->_FringeScale * 3.0f",
                "const float endpoint_snap_radius =",
                "triangle_r * 0.25f,",
                "style.GrabMinSize * 0.50f,",
                "wheel_thickness * 0.75f",
                "const float authored_marker_radius =",
                "ImLerp(compact_marker_radius, endpoint_snap_radius, 0.5f)",
                "endpoint_snap_radius,",
                "authored_marker_radius,",
                "else if (initial_dist2 >=",
                "value_changed = value_changed_h = true;",
                "? final_bar_pos_x",
                "bars_width - picker_pos.x);",
                "ImGuiColorEditFlags_NoSmallPreview;",
                "RenderUvsrRoundedTriangleSnapPoints("
            },
            "selector endpoints are routed before the hue annulus, every authored "
            "picker allocates four fourth-column-aligned lanes, and one exact "
            "midpoint radius feeds the visible marker system");
        RequireAbsent(
            finalVerticalMarker,
            "AddCircleFilled(",
            "authored hue and opacity indicators remain hollow circles");
        RequireOrdered(
            imguiTooltipPickerAdded,
            {
                "const bool authored_disabled_alpha_component =",
                "(flags & ImGuiColorEditFlags_NoPicker) != 0 &&",
                "(flags & ImGuiColorEditFlags_NoSmallPreview) != 0;",
                "authored_disabled_alpha_component ? 4 : components;",
                "if (n == components)",
                "BeginDisabled();",
                "disabled_alpha_value,",
                "EndDisabled();",
                "const int authored_bar_count = authored_bar_popup ? 4 : 0;",
                "ImGuiColorEditFlags_NoPicker |",
                "ImGuiColorEditFlags_NoSmallPreview;",
                "else if (render_uvsr_wheel_with_bars)",
                "IM_COL32(112, 112, 112, 255);",
                "disabled_alpha_stops",
                "RenderFrameBorder("
            },
            "authored subordinate RGB/HSV rows remove their preview squares, "
            "reserve a disabled fourth value, and render a noninteractive gray "
            "alpha lane without accessing a fourth caller component");
        RequireOrdered(
            imguiTooltipPickerAdded,
            {
                "const float authored_marker_radius =",
                "? ImLerp(compact_marker_radius, endpoint_snap_radius, 0.5f)",
                "const float sv_cursor_rad = render_uvsr_wheel_with_bars",
                "? authored_marker_radius",
                "hue_cursor_rad = render_uvsr_wheel_with_bars",
                "? authored_marker_radius",
                "RenderUvsrHollowMarkerForVerticalBar(",
                "uvsr_hue_bar_line_y,",
                "authored_marker_radius,",
                "uvsr_alpha_bar_line_y,",
                "authored_marker_radius,"
            },
            "the midpoint radius is shared by endpoint, SV, hue-wheel, hue-bar, "
            "and alpha-bar circles without authored active-state growth");

        const std::string_view comparisonBars = ExtractSection(
            imguiTooltipPickerAdded,
            "const ImU32 comparison_colors[2] =",
            "const ImRect hue_bar_bounds(",
            "display-only current and original color bars");
        RequireOrdered(
            comparisonBars,
            {
                "R,",
                "G,",
                "B,",
                "ImSaturate(col[3])",
                "reference_red,",
                "reference_green,",
                "reference_blue,",
                "ImSaturate(ref_col[3])",
                "comparison_current_pos_x,",
                "comparison_original_pos_x",
                "AddUvsrRoundedCheckerboard(",
                "AddUvsrRoundedVerticalGradient(",
                "RenderFrameBorder("
            },
            "unlabeled Current and Original values render as two opaque, "
            "checker-backed comparison bars after hue and optional alpha");
        for (const std::string_view retiredComparisonControl : {
                std::string_view("Text(\"Current\")"),
                std::string_view("Text(\"Original\")"),
                std::string_view("InvisibleButton(\"##current"),
                std::string_view("ColorButton(\"##current") })
        {
            RequireAbsent(
                imguiTooltipPickerAdded,
                retiredComparisonControl,
                "retired labeled or interactive comparison preview");
        }

        const std::string_view authoredSwatch = ExtractSection(
            imguiTooltipPickerOverride,
            "const bool authored_color_swatch =",
            "RenderNavCursor(bb, id);",
            "authored color swatch surface");
        RequireOrdered(
            authoredSwatch,
            {
                "!g.UvsrStockWidgetRendering &&",
                "g.UvsrColorPickerPopupScopeStack.Size > 0;",
                "if (authored_color_swatch)",
                "ImGuiCol_BorderShadow",
                "shadow_color.w = ImMax(shadow_color.w, 0.42f);",
                "const ImVec2 shadow_offset(",
                "window->DrawList->AddRectFilled(",
                "GetColorU32(shadow_color),",
                "else if ((flags & ImGuiColorEditFlags_NoBorder) == 0)"
            },
            "only the authored scoped swatch receives an offset soft shadow "
            "before its color surface");
        RequireOrdered(
            imguiTooltipPickerOverride,
            {
                "RenderNavCursor(bb, id);",
                "if (!authored_color_swatch &&",
                "(flags & ImGuiColorEditFlags_NoBorder) == 0)",
                "RenderFrameBorder(bb.Min, bb.Max, rounding);"
            },
            "the authored color swatch suppresses only its perimeter while stock "
            "and unscoped ColorButton borders remain unchanged");

        const std::string_view roundedCheckerPolish = ExtractSection(
            imguiUiPolishOverride,
            "static void AddUvsrRoundedCheckerboard(",
            "static void AddUvsrRoundedVerticalGradient(",
            "shared rounded alpha checkerboard mask");
        RequireOrdered(
            roundedCheckerPolish,
            {
                "rounding = ImClamp(",
                "const ImVec4 colors[2] =",
                "ImVector<float> x_coordinates;",
                "const float maximum_mesh_step = ImMax(fringe, 1.0f);",
                "bounds.Min.x - fringe * 0.5f",
                "bounds.Max.y + fringe * 0.5f",
                "x_coordinates.Size * y_coordinates.Size",
                "const float distance = GetUvsrRoundedRectDistance(",
                "bounds,",
                "rounding,",
                "position);",
                "color.w *= coverage;",
                "draw_list->PrimWriteVtx("
            },
            "both checker colors share one rounded-rect distance mask instead "
            "of assigning a brittle radius to the final partial cell, while "
            "the per-cell mesh keeps checker seams hard and the edge transition "
            "within the antialias fringe");
        RequireOrdered(
            imguiUiPolishOverride,
            {
                "AddUvsrRoundedCheckerboard(",
                "bar1_bb,",
                "uvsr_bar_rounding,",
                "color_control_alpha);",
                "const UvsrVerticalGradientStop alpha_stops[2]"
            },
            "the shared rounded checker is submitted before the retained "
            "authored alpha gradient and border path");
        const std::string_view closedComboOverride = ExtractSection(
            imguiUiOverride,
            "bool ImGui::BeginCombo(const char* label",
            "void ImGui::EndCombo()",
            "closed combo presentation override");
        RequireOrdered(
            closedComboOverride,
            {
                "ItemSize(total_bb, style.FramePadding.y);",
                "ItemAdd(total_bb, id, &bb)",
                "GetAnimatedHighlightAmount(",
                "hovered || popup_open",
                "LerpWidgetColor(",
                "ImRect visual_bb = bb;",
                "if (!g.UvsrStockWidgetRendering)",
                "visual_bb.Expand(ImVec2(0.0f, -2.0f));",
                "RenderNavCursor(bb, id);",
                "AddRectFilled(visual_bb.Min",
                "popup_open\n"
                "+                        ? ImGuiCol_ButtonActive",
                "AddRectFilled(ImVec2(value_x2, visual_bb.Min.y), visual_bb.Max",
                "RenderFrameBorder(visual_bb.Min, visual_bb.Max",
                "style.FrameRounding);",
                "g.TextShadowAlphaScale *= 0.55f;",
                "visual_bb.Min.x + style.FramePadding.x",
                "visual_bb.Max.y"
            },
            "closed authored combos preserve the full item/hit rectangle while "
            "insetting only their visible frame by two pixels on each Y edge");
        RequireAbsent(
            closedComboOverride,
            "visual_bb.Expand(ImVec2(-2.0f, 0.0f))",
            "closed combo visual inset must not change its hit width");

        const std::string_view keyboardUpdate = ExtractSection(
            viewer,
            "protected:\n    virtual bool KeyboardUpdate(",
            "virtual bool KeyboardCharInput(",
            "Settings keyboard shortcuts");
        const std::string_view settingsShortcutOwnership = ExtractSection(
            keyboardUpdate,
            "const bool settingsShortcutOwnedByUi =",
            "const bool plainCommandShortcut =",
            "Settings shortcut ownership predicate");
        const std::string compactSettingsShortcutOwnership =
            Compact(settingsShortcutOwnership);
        for (const std::string_view ownershipContract : {
                std::string_view("ImGui::GetIO().WantTextInput"),
                std::string_view("ImGui::IsAnyItemActive()"),
                std::string_view(
                    "ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)") })
        {
            RequireContains(
                compactSettingsShortcutOwnership,
                ownershipContract,
                "Settings shortcut ownership predicate");
        }
        const std::string_view settingsShortcut = ExtractSection(
            keyboardUpdate,
            "if ((key == GLFW_KEY_ESCAPE ||",
            "const bool plainFlashlightShortcut =",
            "Settings shortcut ownership gate");
        const std::string compactSettingsShortcut = Compact(settingsShortcut);
        for (const std::string_view shortcutContract : {
                std::string_view("key==GLFW_KEY_ESCAPE"),
                std::string_view("key==GLFW_KEY_GRAVE_ACCENT"),
                std::string_view("action==GLFW_PRESS"),
                std::string_view("!settingsShortcutOwnedByUi") })
        {
            RequireContains(
                compactSettingsShortcut,
                shortcutContract,
                "Escape and tilde Settings shortcut gate");
        }
        RequireAbsent(
            settingsShortcut,
            "mods",
            "shift-compatible grave-accent Settings shortcut");
        RequireContains(
            keyboardUpdate,
            "m_ui.ShowUI = !m_ui.ShowUI;",
            "Settings shortcut visibility toggle");
        RequireContains(
            viewer,
            "ImGui::CalcTextSize(\n"
            "                \"Bitmask Directional Visibility\")",
            "Settings width sized for longest Diffuse estimator label");

        const std::string_view generalDrawer = ExtractSection(
            viewer,
            "void DrawGeneralDrawer(float settingsControlWidth)",
            "void DrawMaterialDrawer(float settingsControlWidth)",
            "General drawer");
        RequireOrdered(
            generalDrawer,
            {
                "\"Graphics Adapter\"",
                "\"Adaptive Sync\"",
                "\"Camera Mode\"",
                "\"Camera Location\"",
                "\"World Scenes\"",
                "\"##OpenSceneFolder\"",
                "\"Retry Scene Load\""
            },
            "complete General control order");
        Require(
            CountOccurrences(
                generalDrawer,
                "DrawDeferredDropdownOption(") == 5u,
            "General must keep adapter, Adaptive Sync, camera, location, and "
            "scene selection on the deferred dropdown path.");
        RequireOrdered(
            generalDrawer,
            {
                "[this, adapterIndex = adapter.adapterIndex]()",
                "g_RestartAdapterIndex = adapterIndex;",
                "g_RestartRequested = true;",
                "glfwSetWindowShouldClose(",
                "QueueDeferredControlUiAction(",
                "ApplyAdaptiveSyncMode(",
                "[this, candidate]()",
                "ApplyAdaptiveSyncMode(candidate);",
                "m_app->SetCameraMode(mode);",
                "m_app->SetCameraLocation(candidate);",
                "[this, sceneName = scene.FileName]()",
                "m_app->SetCurrentSceneName(sceneName);",
                "ShellExecuteW(",
                "m_app->RetryCurrentSceneLoad();"
            },
            "General callback and restart semantics");
        for (const std::string_view adaptiveControlContract : {
                std::string_view("AdaptiveSyncModeValues"),
                std::string_view(
                    "IsAdaptiveSyncModeAvailableForActiveAdapter"),
                std::string_view("ImGui::BeginDisabled();"),
                std::string_view(
                    "Request the shared Windows variable-refresh path"),
                std::string_view(
                    "whether it engages."),
                std::string_view("VSync stays off"),
                std::string_view(
                    "controls adaptive refresh"),
                std::string_view("which UVSR cannot verify") })
        {
            RequireContains(
                generalDrawer,
                adaptiveControlContract,
                "honest Adaptive Sync selector");
        }
        RequireAbsent(
            generalDrawer,
            "NVIDIA-only variable-refresh",
            "shared-path Nvidia Exclusive explanation");

        const std::string_view skyDrawer = ExtractSection(
            viewer,
            "const bool skyOpen = DrawCollapsingHeader(",
            "const bool lightsOpen = DrawCollapsingHeader(",
            "Sky drawer");
        RequireContains(
            skyDrawer,
            "Disable it to \"\n"
            "                \"isolate direct lights; settings persist, and Occlusion",
            "renamed Occlusion ambient-fill explanation");
        RequireAbsent(
            skyDrawer,
            "Ambient-occlusion settings",
            "renamed Occlusion ambient-fill explanation");
        RequireOrdered(
            skyDrawer,
            {
                "Environment Exposure",
                "\"Auto Exposure##Sky\"",
                "ImGuiTreeNodeFlags_DefaultOpen",
                "\"Enable##AutoExposure\"",
                "&m_ui.AutoExposure.enabled",
                "##AutoExposureControls",
                "\"Exposure Compensation\"",
                "&m_ui.AutoExposure.exposureCompensationEV",
                "AutoExposureMinimumCompensationEV",
                "AutoExposureMaximumCompensationEV",
                "\"%+.2f EV\"",
                "\"Maximum Brightening\"",
                "&m_ui.AutoExposure.maximumBrighteningEV",
                "AutoExposureMinimumMovementEV",
                "AutoExposureMaximumMovementEV",
                "\"Maximum Darkening\"",
                "&m_ui.AutoExposure.maximumDarkeningEV",
                "AutoExposureMinimumMovementEV",
                "AutoExposureMaximumMovementEV",
                "\"Adjustment Period\"",
                "&m_ui.AutoExposure.adjustmentPeriodSeconds",
                "AutoExposureMinimumAdjustmentPeriodSeconds",
                "AutoExposureMaximumAdjustmentPeriodSeconds",
                "\"%.2f s\"",
                "EndAnimatedToggleRegion();",
                "EndAnimatedTreeNode();",
                "\"Ambient Fill\""
            },
            "Sky Auto Exposure submenu and enabled-only control order");
        RequireContains(
            skyDrawer,
            "if (BeginAnimatedTreeNode(\n"
            "                    \"Auto Exposure##Sky\",\n"
            "                    ImGuiTreeNodeFlags_DefaultOpen,",
            "Auto Exposure submenu must start expanded like AA techniques");
        RequireContains(
            Compact(skyDrawer),
            "if(BeginAnimatedToggleRegion(\"##AutoExposureControls\","
                "m_ui.AutoExposure.enabled)){",
            "Auto Exposure settings must stay hidden while Enable is off");
        RequireContains(
            skyDrawer,
            "Adapt display exposure to the median scene luminance.",
            "auto-exposure display-only explanation");
        RequireContains(
            skyDrawer,
            "Exposure Compensation is applied afterward.",
            "automatic movement limits exclude Exposure Compensation");
        RequireContains(
            skyDrawer,
            "Set the half-life of exposure adaptation.",
            "Adjustment Period half-life explanation");
        RequireOrdered(
            skyDrawer,
            {
                "Show Environment Background",
                "Ray Traced Sky Visibility##Sky",
                "Enable##RayTracedSkyVisibility",
                "Effect Diffuse##RayTracedSkyVisibility",
                "Effect Specular##RayTracedSkyVisibility",
                "Ratio Estimator##RayTracedSkyVisibility",
                "Output Hit Distance##RayTracedSkyVisibility",
                "Samples Per Pixel##RayTracedSkyVisibility",
                "Specify Noise##RayTracedSkyVisibility",
                "Max Distance##RayTracedSkyVisibility",
                "Ray Bias##RayTracedSkyVisibility"
            },
            "bottom-of-Sky ray-traced visibility controls");
        RequireContains(
            skyDrawer,
            "if (BeginAnimatedTreeNode(\n"
            "                    \"Ray Traced Sky Visibility##Sky\"",
            "independently collapsible sky-visibility effect section");
        RequireContains(
            skyDrawer,
            "skyVisibility.enabled && skyVisibilityAvailable",
            "enabled sky-visibility settings region");
        RequireOrdered(
            skyDrawer,
            {
                "BeginAnimatedTreeNode(",
                "RayTracedSkyVisibilitySettings& skyVisibility",
                "EndAnimatedTreeNode();"
            },
            "sky visibility collapsibility independent of enabled state");
        RequireAbsent(
            skyDrawer,
            "Diffuse IBL##RayTracedSkyVisibility",
            "renamed Effect Diffuse control");
        RequireAbsent(
            skyDrawer,
            "Specular IBL##RayTracedSkyVisibility",
            "renamed Effect Specular control");

        const std::string_view lightsDrawer = ExtractSection(
            viewer,
            "const bool lightsOpen = DrawCollapsingHeader(",
            "const bool shadowsOpen = DrawCollapsingHeader(",
            "Lights drawer");
        RequireOrdered(
            lightsDrawer,
            {
                "\"Beam Size\"",
                "&flashlight.beamSizeDegrees",
                "\"Angular Size\"",
                "&flashlight.angularSizeDegrees",
                "FlashlightMinimumAngularSizeDegrees",
                "FlashlightMaximumAngularSizeDegrees"
            },
            "flashlight analytical Angular Size control");
        RequireContains(
            lightsDrawer,
            "Set the apparent diameter of the analytical ",
            "flashlight analytical emitter tooltip");
        RequireContains(
            lightsDrawer,
            "ImGui::Checkbox(\n"
                "                                \"Enabled\",\n"
                "                                &m_ui.FlashlightEnabled)",
            "flashlight enable label without redundant shortcut suffix");
        RequireAbsent(
            lightsDrawer,
            "Enabled (F)",
            "retired flashlight shortcut suffix");
        RequireOrdered(
            lightsDrawer,
            {
                "bool angularSizeChanged = DrawBoundedSliderFloat(",
                "angularSizeChanged = true;",
                "if (angularSizeChanged)",
                "m_app->ResetImageBasedLightingHistory();"
            },
            "flashlight Angular Size UI history reset");
        for (const std::string_view retiredMovementUi : {
                std::string_view("\"Adjustment Speed\""),
                std::string_view("\"Time to Action\""),
                std::string_view(
                    "\"Camera Movement Diagnostics##Flashlight\"") })
        {
            RequireAbsent(
                lightsDrawer,
                retiredMovementUi,
                "retired flashlight camera-centering UI stays absent");
        }

        const std::string_view footer = ExtractSection(
            viewer,
            "constexpr float ActionButtonCount = 4.f;",
            "EndSettingsScrollStability();",
            "Settings footer actions");
        RequireContains(
            footer,
            "DrawCenteredActionButton(\"Capture\", actionButtonWidth)",
            "Capture footer action");
        RequireAbsent(
            footer,
            "DrawCenteredActionButton(\"Screenshot\"",
            "retired Screenshot footer label");

        const std::string_view generalDispatcher = ExtractSection(
            viewer,
            "bool DispatchGeneralCommandValue(",
            "bool DispatchRepresentationCommandValue(",
            "General command dispatcher");
        const std::string_view adaptiveDispatcher = ExtractSection(
            generalDispatcher,
            "if (path == \"gpu.adaptive-sync\")",
            "if (path == \"camera.mode\")",
            "Adaptive Sync command dispatcher");
        RequireOrdered(
            adaptiveDispatcher,
            {
                "{ \"off\", AdaptiveSyncMode::Off }",
                "{ \"vendor-agnostic\", AdaptiveSyncMode::VendorAgnostic }",
                "{ \"nvidia-exclusive\", AdaptiveSyncMode::NvidiaExclusive }",
                "IsAdaptiveSyncModeAvailableForActiveAdapter(candidate)",
                "ApplyAdaptiveSyncMode(candidate)"
            },
            "Adaptive Sync command behavior");
        for (const std::string_view commandContract : {
                std::string_view("operation"),
                std::string_view("arguments"),
                std::string_view("GetDefaultAdaptiveSyncMode()"),
                std::string_view(
                    "operation == CommandValueOperation::Get"),
                std::string_view(
                    "IsAdaptiveSyncModeAvailableForActiveAdapter(candidate)"),
                std::string_view(
                    "Nvidia Exclusive requires an NVIDIA graphics adapter."),
                std::string_view(
                    "Adaptive Sync requires DXGI tearing-present support.") })
        {
            RequireContains(
                adaptiveDispatcher,
                commandContract,
                "Adaptive Sync get/set/reset and rejection command path");
        }
        for (const std::string_view presentContract : {
                std::string_view("m_RequestedPresentAllowTearing = true"),
                std::string_view("m_PresentAllowTearingSupported = false"),
                std::string_view("SetPresentAllowTearing(bool enabled)"),
                std::string_view(
                    "m_PresentAllowTearingSupported = m_TearingSupported"),
                std::string_view("m_RequestedPresentAllowTearing"),
                std::string_view(
                    "presentFlags |= DXGI_PRESENT_ALLOW_TEARING") })
        {
            RequireContains(
                donutAppOverride,
                presentContract,
                "runtime Adaptive Sync Present policy");
        }
        const std::string_view dx12Present = ExtractSection(
            donutAppOverride,
            "@@ -576,7 +577,10 @@ bool DeviceManager_DX12::Present()",
            "HRESULT result = m_SwapChain->Present",
            "patched DX12 Present policy");
        RequireContains(
            Compact(dx12Present),
            "+if(!m_DeviceParams.vsyncEnabled&&+m_FullScreenDesc.Windowed&&"
            "+m_TearingSupported&&+m_RequestedPresentAllowTearing)",
            "conjunctive DX12 Present tearing policy");
        const std::string_view adaptiveStartup = ExtractSection(
            viewer,
            "const auto activeAdapter = std::find_if(",
            "auto demo = std::make_shared<UvsrSceneViewer>(",
            "Adaptive Sync startup policy");
        RequireOrdered(
            adaptiveStartup,
            {
                "DefaultAdaptiveSyncMode(",
                "deviceManager->SetPresentAllowTearing("
            },
            "Adaptive Sync startup default application");
        Require(
            CountOccurrences(
                cmakeSource,
                "src/app/dx12/DeviceManager_DX12.cpp") == 2u,
            "CMake must stage and compile the patched DX12 DeviceManager.");
        RequireContains(
            cmakeSource,
            "list(FIND target_sources \"${relative_path}\" source_index)",
            "target-relative Donut backend source replacement");

        const std::string_view resetIcon = ExtractSection(
            viewer,
            "static bool DrawPresetResetIconAtPlacement(",
            "static bool DrawPresetResetIcon(",
            "preset reset icon renderer");
        RequireContains(
            Compact(resetIcon),
            "constboolpressed=ImGui::Button(\"##PresetReset\","
            "ImVec2(buttonSize,buttonSize));",
            "native preset reset button frame");
        RequireAbsent(
            resetIcon,
            "ImGui::InvisibleButton(",
            "preset reset manual interaction surface");
        RequireAbsent(
            resetIcon,
            "AddRectFilled",
            "preset reset manual background");

        const std::string_view sceneFolder = ExtractSection(
            viewer,
            "ImGui::TextUnformatted(\"World Scenes\");",
            "EndDrawerBody();",
            "scene folder control");
        RequireContains(
            Compact(sceneFolder),
            "constboolopenSceneFolderPressed=ImGui::Button("
            "\"##OpenSceneFolder\",ImVec2(folderButtonWidth,"
            "ImGui::GetFrameHeight()));",
            "native scene folder button frame");
        RequireAbsent(
            sceneFolder,
            "ImGui::InvisibleButton(",
            "scene folder manual interaction surface");
        RequireAbsent(
            sceneFolder,
            "AddRectFilled",
            "scene folder double-composited backgrounds");
        RequireOrdered(
            sceneFolder,
            {
                "if (m_SceneLoadFailed)",
                "\"Retry Scene Load\"",
                "m_app->RetryCurrentSceneLoad();"
            },
            "failed scene load retry action");
        RequireAbsent(
            viewer,
            "folderOutline",
            "retired scene folder manual outline token");

        const std::string_view nativeFrame = ExtractSection(
            imguiUiOverride,
            "void ImGui::RenderFrame(",
            "void ImGui::RenderGradientFrame(",
            "staged native frame renderer");
        RequireContains(
            nativeFrame,
            "RenderGradientFrameOutline(",
            "carved native-frame gradient outline");
        RequireContains(
            nativeFrame,
            "rounding,\n+        false);",
            "default carved native-frame depth role");
        const std::string_view outlineRenderer = ExtractSection(
            imguiUiOverride,
            "static void RenderGradientFrameOutline(",
            "// Render a rectangle shaped with optional rounding and borders",
            "base raised and carved outline renderer");
        for (const std::string_view depthContract : {
                std::string_view("bool raised"),
                std::string_view("bool dark_outline = false"),
                std::string_view(
                    "ImDrawFlags rounding_flags = ImDrawFlags_RoundCornersAll"),
                std::string_view(
                    "const ImVec4 raised_top(1.0f, 1.0f, 1.0f, 0.12f);"),
                std::string_view(
                    "const ImVec4 raised_bottom(0.01f, 0.012f, 0.016f, 0.10f);"),
                std::string_view(
                    "const ImVec4 carved_top(0.005f, 0.006f, 0.008f, 0.14f);"),
                std::string_view(
                    "const ImVec4 carved_bottom(0.88f, 0.90f, 0.94f, 0.070f);"),
                std::string_view(
                    "const ImVec4 bright_top(0.0f, 0.0f, 0.0f, 0.24f);"),
                std::string_view(
                    "const ImVec4 bright_bottom(0.16f, 0.17f, 0.19f, 0.32f);"),
                std::string_view(
                    "const ImVec4& top_color = dark_outline"),
                std::string_view("? bright_top"),
                std::string_view(": raised ? raised_top : carved_top;"),
                std::string_view(
                    "const ImVec4& bottom_color = dark_outline"),
                std::string_view("? bright_bottom"),
                std::string_view(": raised ? raised_bottom : carved_bottom;"),
                std::string_view(
                    "outline_color.w *= float((vertex.col & IM_COL32_A_MASK)"),
                std::string_view("vertex.col = ImGui::GetColorU32(outline_color);"),
                std::string_view(
                    "IsUltraBrightFrameSurface(top_color)"),
                std::string_view("? 0.025f"),
                std::string_view(": 0.045f;"),
                std::string_view("bottom_color.w *= 0.98f;") })
        {
            RequireContains(
                outlineRenderer,
                depthContract,
                "base subdued raised-versus-carved outline and subtle fill contract");
        }
        const std::string_view finalCarvedOutline = ExtractSection(
            imguiTooltipPickerAdded,
            "ImU32 ImGui::GetUvsrCarvedFrameOutlineColor(",
            "static ImGuiID GetUvsrTooltipOwnerId()",
            "final alpha-respecting carved outline override");
        RequireOrdered(
            finalCarvedOutline,
            {
                "ImU32 ImGui::GetUvsrCarvedFrameOutlineColor(",
                "const ImVec4 carved_top(0.005f, 0.006f, 0.008f, 0.14f);",
                "const ImVec4 carved_bottom(0.88f, 0.90f, 0.94f, 0.070f);",
                "outline_color.w *= ImSaturate(coverage);",
                "return GetColorU32(outline_color);",
                "const float coverage =",
                "if (!raised && !dark_outline)",
                "vertex.col = ImGui::GetUvsrCarvedFrameOutlineColor(",
                "gradient_position,",
                "coverage);",
                "continue;"
            },
            "carved reset/control outlines honor both antialias coverage and "
            "the active global/style alpha conversion");
        Require(
            CountOccurrences(
                imguiUiOverride,
                "IsUltraBrightFrameSurface(top_color)") == 2u,
            "core and widget raised gradients must both shade from their "
            "actual surface luminance");
        RequireContains(
            imguiUiOverride,
            "return surface.w > 0.0f && luminance >= 0.68f;",
            "a zero-alpha packed surface must not select the automatic dark "
            "outline");
        const std::string_view gradientFrame = ExtractSection(
            imguiUiOverride,
            "void ImGui::RenderGradientFrame(",
            "void ImGui::RenderFrameBorder(",
            "semantic gradient-frame renderer");
        RequireOrdered(
            gradientFrame,
            {
                "bool raised",
                "ImDrawFlags rounding_flags",
                "AddRectFilled(",
                "IM_COL32_WHITE",
                "rounding_flags);",
                "const float coverage =",
                "fill_color.w *= coverage;",
                "const bool dark_outline =",
                "raised &&",
                "IsUltraBrightFrameSurface(top_col);",
                "RenderGradientFrameOutline(",
                "raised,",
                "dark_outline,",
                "rounding_flags);"
            },
            "rounded AA-preserving semantic gradient-frame routing");
        RequireContains(
            imguiUiOverride,
            "bool raised = false, "
            "ImDrawFlags rounding_flags = ImDrawFlags_RoundCornersAll);",
            "default-carved, automatic bright-outline, and selective-corner "
            "gradient-frame API");
        RequireAbsent(
            imguiUiOverride,
            "bool raised = false, bool dark_outline = false,",
            "retired caller-selected bright-outline API");
        RequireContains(
            imguiUiOverride,
            "GetRaisedFrameGradientBottomColor(header_color)",
            "raised drawer-header tiny fill gradient");
        RequireContains(
            imguiUiOverride,
            "GetRaisedFrameGradientBottomColor(title_bar_col)",
            "raised Settings-title tiny fill gradient");
        const std::string_view stackedPanelCorners = ExtractSection(
            imguiUiOverride,
            "const bool is_stacked_panel_title =",
            "// Menu bar",
            "independent root-panel corner routing");
        RequireOrdered(
            stackedPanelCorners,
            {
                "strcmp(window->Name, \"Settings\") == 0 || strcmp(window->Name, \"Performance\") == 0",
                "ImDrawFlags_RoundCornersAll",
                "expanded title and body as independent fully",
                "window->TitleBarHeight - 1.0f",
                "ImDrawFlags_RoundCornersAll",
                "both anti-aliased side arcs inside the window clip",
                "ImDrawFlags_RoundCornersAll",
                "independent body's complete rounded depth outline",
                "ImDrawFlags_RoundCornersAll"
            },
            "Performance and Settings keep fully rounded independent surfaces");
        const std::string_view expandedStackedBody = ExtractSection(
            imguiUiOverride,
            "expanded_stacked_panel_body_rect = ImRect(",
            "// Title bar",
            "authored-versus-stock root body rounding");
        RequireOrdered(
            expandedStackedBody,
            {
                "expanded_stacked_panel_body_rect = ImRect(",
                "window->WindowRounding,",
                "ImDrawFlags_RoundCornersAll);",
                "else",
                "window_rounding",
                "ImDrawFlags_RoundCornersBottom"
            },
            "Performance and Settings bodies use semantic WindowRounding while "
            "ordinary windows retain the untouched stock window-rounding path");
        RequireOrdered(
            stackedPanelCorners,
            {
                "RenderGradientFrame(",
                "collapsed_title_rect.Min,",
                "style.FrameRounding",
                "ImDrawFlags_RoundCornersAll",
                "RenderGradientFrame(",
                "title_fill_rect.Min,",
                "style.FrameRounding,",
                "ImDrawFlags_RoundCornersAll"
            },
            "collapsed and expanded authored root titles retain semantic "
            "FrameRounding while sharing the authored body's resolved radius");
        RequireOrdered(
            stackedPanelCorners,
            {
                "if (has_retained_status_surface &&",
                "!g.UvsrStockWidgetRendering)",
                "window->WindowBorderSize = 0.0f;",
                "RenderGradientFrameOutline(",
                "retained_status_rect,",
                "if (has_retained_status_surface &&",
                "g.UvsrStockWidgetRendering)",
                "RenderWindowOuterBorders(window);"
            },
            "authored compact Performance uses separate carved-body and raised-title "
            "depth outlines while Ogg retains one stock outer border around its "
            "title and summary");
        RequireAbsent(
            stackedPanelCorners,
            "? ImDrawFlags_RoundCornersTop",
            "retired seam-square Performance corner selection");
        RequireAbsent(
            stackedPanelCorners,
            "? ImDrawFlags_RoundCornersBottom",
            "retired seam-square Settings corner selection");
        RequireContains(
            viewer,
            "const float settingsWindowTop =\n"
            "            performanceWindowPosition.y +\n"
            "            performanceWindowSize.y +\n"
            "            panelSeparation;",
            "Performance and Settings retain one exact ordinary drawer gap");
        const std::string_view settingsScrollBegin = ExtractSection(
            viewer,
            "static void BeginSettingsScrollStability()",
            "static void TrackSettingsScrollAnchor(",
            "Settings wheel-owner capture");
        RequireOrdered(
            settingsScrollBegin,
            {
                "const ImGuiContext* imguiContext =",
                "const bool settingsBodyConsumedWheel =",
                "imguiContext->WheelingWindow ==",
                "ImGui::GetCurrentWindow()",
                "imguiContext->WheelingWindowScrolledFrame == frame;",
                "context.wheelInput = settingsBodyConsumedWheel",
                "? ImGui::GetIO().MouseWheel",
                ": 0.f;"
            },
            "only wheel input actually consumed by the current Settings child "
            "may lock its scroll endpoint");
        const std::string_view scrollAnchorCorrection = ExtractSection(
            uiAnimation,
            "ResolveUiScrollAnchorCorrection(",
            "// Retain the Settings viewport",
            "scroll-anchor endpoint correction");
        RequireOrdered(
            scrollAnchorCorrection,
            {
                "if (hasPendingScrollTarget)",
                "return { false, currentScrollY };",
                "const float maximumScrollY =",
                "if (wheelInput > 0.001f && wheelAtTop)",
                "currentScrollY > 0.01f,",
                "0.f",
                "if (wheelInput < -0.001f && wheelAtBottom)",
                "const float endpointDifference =",
                "currentScrollY - maximumScrollY;",
                "endpointDifference > 0.01f ||",
                "endpointDifference < -0.01f,",
                "maximumScrollY",
                "if (scrollDelta >= -0.01f && scrollDelta <= 0.01f)",
                "const float requestedScrollY = currentScrollY + scrollDelta;"
            },
            "pending targets win, outward endpoint wheels lock without a "
            "second correction, and ordinary anchor deltas remain clamped");
        const std::string_view settingsScrollbarInset = ExtractSection(
            imguiUiOverride,
            "const bool settings_body_scrollbar =",
            "// V denote the main",
            "Settings-only scrollbar inset");
        RequireOrdered(
            settingsScrollbarInset,
            {
                "!g.UvsrStockWidgetRendering",
                "axis == ImGuiAxis_Y",
                "strstr(window->Name, \"##SettingsBody\")",
                "const float scrollbar_inset_x =",
                "settings_body_scrollbar",
                "? 1.0f",
                ": ImClamp(",
                "const float scrollbar_inset_y =",
                "settings_body_scrollbar",
                "? 1.0f",
                ": ImClamp(",
                "bb.Expand(ImVec2(",
                "-scrollbar_inset_x,",
                "-scrollbar_inset_y));"
            },
            "authored Settings scrollbar keeps one physical pixel of AA inset "
            "on both axes while other scrollbars use stock inset math");
        Require(
            CountOccurrences(settingsScrollbarInset, "? 1.0f") == 2u &&
                CountOccurrences(settingsScrollbarInset, "ImClamp(") == 2u,
            "Settings must override exactly two one-pixel inset axes without "
            "removing the two stock fallback calculations.");
        RequireContains(
            authoredCornerRounding,
            "style.ScrollbarSize = 12.f;",
            "authored twelve-pixel scrollbar frame producing a ten-pixel "
            "visible Settings grab that preserves true four-pixel rounding");
        RequireContains(
            imguiUiOverride,
            "RenderFrameBorder(\n"
            "+        grab_rect.Min,\n"
            "+        grab_rect.Max,\n"
            "+        style.ScrollbarRounding,\n"
            "+        false);",
            "carved scrollbar depth role");

        const std::string_view commandHeight = ExtractSection(
            viewer,
            "static float GetCommandInterfaceMinimumHeight()",
            "static float GetSettingsCollapsedWindowHeight(",
            "one-row command height");
        RequireContains(
            Compact(commandHeight),
            "returnstd::ceil(style.WindowPadding.y*2.f+"
            "ImGui::GetFrameHeight());",
            "one-row command minimum height");
        RequireContains(
            commandHeight,
            "return GetCommandInterfaceMinimumHeight();",
            "one-row command reserved height");
        const std::string_view collapsedSettingsHeight = ExtractSection(
            viewer,
            "static float GetSettingsCollapsedWindowHeight(",
            "static float GetSettingsMinimumExpandedWindowHeight(",
            "title-only collapsed Settings height");
        RequireContains(
            Compact(collapsedSettingsHeight),
            "returnfontSize+style.FramePadding.y*2.f;",
            "title-only collapsed Settings height");
        for (const std::string_view retiredStatusHeight : {
                std::string_view("SettingsStatusLineSpacing"),
                std::string_view("hasPerformanceStatus"),
                std::string_view("splitOgPerformanceStatus"),
                std::string_view("style.WindowPadding.y"),
                std::string_view("style.ItemSpacing.y") })
        {
            RequireAbsent(
                collapsedSettingsHeight,
                retiredStatusHeight,
                "collapsed Settings retained-status sizing");
        }
        const std::string_view expandedSettingsHeight = ExtractSection(
            viewer,
            "static float GetSettingsMinimumExpandedWindowHeight(",
            "static float AdvanceUiLayoutAnimation(",
            "expanded Settings minimum height");
        RequireContains(
            Compact(expandedSettingsHeight),
            "constfloatframeHeight=fontSize+style.FramePadding.y*2.f;"
            "returnframeHeight*2.f+style.WindowPadding.y*2.f;",
            "expanded Settings title, one usable body row, and both root "
            "padding edges");
        const std::string_view settingsLayoutMinimum = ExtractSection(
            viewer,
            "const float minimumSettingsHeight =",
            "const float panelStackMaximumBottom =",
            "detached panel-stack CLI layout minimum");
        RequireContains(
            Compact(settingsLayoutMinimum),
            "constfloatpanelSeparation=g_UiSpacingTokens.tight;",
            "root panels use the Tight spacing token");
        RequireOrdered(
            settingsLayoutMinimum,
            {
                "GetSettingsMinimumExpandedWindowHeight(",
                "const float minimumPanelStackHeight =",
                "performanceCollapsedHeight +",
                "panelSeparation +",
                "minimumSettingsHeight;",
                "ResolveCommandInterfaceLayout(",
                "float(m_SettingsPanelMarginPixels),",
                "g_UiSpacingTokens.tight,",
                "260.f * m_UiDisplayScale,",
                "minimumPanelStackHeight);"
            },
            "Performance summary endpoint, Tight panel-to-command gap, and "
            "expanded-Settings stack reserve");
        RequireContains(
            Compact(viewer),
            "ResolvePerformanceMaximumWindowHeight("
            "panelStackMaximumBottom,performanceWindowTop,"
            "minimumSettingsHeight,panelSeparation)",
            "four-argument Performance height reserve including the ordinary gap");
        RequireAbsent(
            settingsLayoutMinimum,
            "GetSettingsCollapsedWindowHeight(",
            "collapsed-height coupling in the expanded Settings reserve");
        const std::string_view settingsCollapsedUse = ExtractSection(
            viewer,
            "const float settingsCollapsedHeight =",
            "if (m_SettingsCollapsedRequest)",
            "title-only Settings collapse routing");
        RequireOrdered(
            settingsCollapsedUse,
            {
                "GetSettingsCollapsedWindowHeight(",
                "ImGui::SetNextUvsrWindowCollapsedHeight("
            },
            "title-only Settings collapse routing");
        const std::string_view settingsRootBody = ExtractSection(
            viewer,
            "const bool settingsExpanded = ImGui::Begin(",
            "ImDrawList* settingsBodyDrawList =",
            "Settings root and scrolling-child padding");
        RequireOrdered(
            settingsRootBody,
            {
                "\"Settings\"",
                "if (settingsExpanded)",
                "const float settingsBodyMaxHeight =",
                "ImGui::GetCursorScreenPos().y - style.WindowPadding.y",
                "ImGui::BeginChild(",
                "\"##SettingsBody\""
            },
            "Settings preserves the root top margin, reserves the matching "
            "bottom inset, and starts its scrolling child at General");
        RequireAbsent(
            settingsRootBody,
            "ImGui::SetCursorPosY(",
            "Settings root top-margin cancellation");
        RequireAbsent(
            settingsRootBody,
            "AddExactVerticalUiGap(",
            "child-owned title-to-General padding");
        RequireAbsent(
            settingsRootBody,
            "ImGuiStyleVar_WindowPadding",
            "root Settings WindowPadding override");
        const std::string_view commandInterface = ExtractSection(
            viewer,
            "void DrawCommandInterface()",
            "void DrawPerformancePanelContents(",
            "one-row command interface");
        const std::string_view opaquePanelSurface = ExtractSection(
            viewer,
            "[[nodiscard]] static ImVec4 GetOpaquePanelBodySurface()",
            "inline static constexpr float",
            "shared compact-panel opaque surface");
        RequireOrdered(
            opaquePanelSurface,
            {
                "ImVec4 surface = g_UiVisualTokens.panelBodySurface;",
                "surface.w = 1.f;",
                "return surface;",
                "static void PushOpaquePanelBodySurface()",
                "ImGuiCol_WindowBg,",
                "GetOpaquePanelBodySurface());"
            },
            "opaque compact-panel surface preserves body RGB while forcing "
            "the resting endpoint opaque");
        RequireContains(
            commandInterface,
            "PushOpaquePanelBodySurface();",
            "CLI compact Performance surface parity");
        RequireContains(
            commandInterface,
            "m_CommandBuffer.front() == '\\0'",
            "command guidance and in-input result gate");
        RequireAbsent(
            commandInterface,
            "##UvsrCommandResult",
            "removed floating command result bar");
        RequireContains(
            commandInterface,
            "ImGui::InputTextWithHint(",
            "one-row command input");
        RequireContains(
            commandInterface,
            "Try help / Enter applies / Tab completes / Up/Down history / Slash closes",
            "slash-separated in-input command guidance");
        RequireContains(
            viewer,
            "m_CommandResult = error ? \"Error: \" : \"Success: \";",
            "explicit command outcome prefix");
        RequireContains(
            commandInterface,
            "ImGuiCol_TextDisabled",
            "in-input command outcome coloring");
        RequireContains(
            commandInterface,
            "g_UiVisualTokens.successText",
            "successful command color");
        RequireContains(
            commandInterface,
            "g_UiVisualTokens.errorText",
            "unsuccessful command color");
        RequireContains(
            commandInterface,
            "const bool commandEdited = ImGui::IsItemEdited();",
            "command edit detection");
        RequireContains(
            commandInterface,
            "if (commandEdited)\n            m_CommandResult.clear();",
            "command result clears when the next input begins");
        RequireContains(
            commandInterface,
            "ImGuiInputTextFlags_CallbackHistory",
            "command recall retention");
        RequireContains(
            commandInterface,
            "const bool commandResultNeedsDetails = showCommandResult &&",
            "long command result detection");
        RequireContains(
            commandInterface,
            "ImGui::OpenPopup(\"##CommandResultDetailsPopup\")",
            "user-invoked command result details");
        RequireContains(
            commandInterface,
            "ImGui::InputTextMultiline(",
            "scrollable and selectable command result details");
        RequireContains(
            commandInterface,
            "ImGuiInputTextFlags_ReadOnly",
            "read-only command result details");
        RequireContains(
            Compact(viewer),
            "renderer->RecallCommandHistory(data,"
            "data->EventKey==ImGuiKey_UpArrow);",
            "up-and-down command recall callback");
        RequireContains(
            Compact(commandInterface),
            "constImVec2commandAppearancePivot("
            "commandWindowPosition.x+commandWindowSize.x*0.5f,"
            "commandWindowBottom);",
            "bottom-center command appearance pivot");
        RequireOrdered(
            commandInterface,
            {
                "const float commandAppearanceScale = commandAppearanceOpacity;",
                "const ImVec2 commandWindowPosition =",
                "const ImVec2 commandWindowSize =",
                "const float commandWindowBottom =",
                "const ImVec2 commandAppearancePivot(",
                "CaptureCurrentWindowBackdrop(",
                "ApplyBackdropAppearance(",
                "commandAppearancePivot,",
                "commandAppearanceScale,",
                "commandAppearanceOpacity);",
                "ImGui::End();",
                "ApplyWindowAppearance(",
                "commandWindowDrawList,",
                "commandAppearancePivot,",
                "commandAppearanceScale,",
                "commandAppearanceOpacity);"
            },
            "command draw vertices and backdrop scale uniformly around one "
            "bottom-center pivot after retaining the full layout lane");

        Require(
            CountOccurrences(viewer, "ImGui::Selectable(") == 1u,
            "all Settings dropdown choices must route through the one "
            "deferred selection wrapper.");
        const std::string_view deferredDropdownState = ExtractSection(
            viewer,
            "struct DeferredDropdownUiState",
            "inline static DeferredDropdownUiState",
            "originating combo-transition deferred dropdown state");
        RequireOrdered(
            deferredDropdownState,
            {
                "DeferredUiActionQueue<ImGuiID, DeferredDropdownUiPayload> actions;",
                "ImGuiID transitionComboId = 0;",
                "int transitionComboLastSubmittedFrame = -1;",
                "double lastRequestTime = 0.0;",
                "int requestFrame = -1;",
                "int idleStartFrame = -1;"
            },
            "deferred dropdown exact transition identity and idle barrier state");
        const std::string_view popupTransition = ExtractSection(
            viewer,
            "static void CancelDeferredDropdownUiActions()",
            "static void DrawTranslucentHeaderPanelBodySurface(",
            "originating combo transition lifecycle");
        RequireOrdered(
            popupTransition,
            {
                "static void CancelDeferredDropdownUiActions()",
                "ImGui::FinishComboPopupTransition(",
                "g_DeferredDropdownUiState.transitionComboId);",
                "g_DeferredDropdownUiState = {};",
                "static bool IsDeferredDropdownPopupTransitionActive()",
                "return ImGui::IsComboPopupTransitionActive(",
                "g_DeferredDropdownUiState.transitionComboId);",
                "static void FinishUnsubmittedDeferredDropdownPopupTransition()",
                "state.actions.Empty() ||",
                "state.transitionComboId == 0 ||",
                "state.transitionComboLastSubmittedFrame ==",
                "ImGui::GetFrameCount()",
                "ImGui::FinishComboPopupTransition(state.transitionComboId);"
            },
            "cancellation and an unsubmitted owner finish only the exact retained "
            "combo transition instead of inferring popup dismissal");
        for (const std::string_view retiredDismissalState : {
                std::string_view("originatingComboId"),
                std::string_view("originatingComboLastSubmittedFrame"),
                std::string_view("popupDismissedFrame"),
                std::string_view("IsDeferredDropdownPopupDismissed"),
                std::string_view("ResolveUnsubmittedDeferredDropdownPopupDismissal") })
        {
            RequireAbsent(
                viewer,
                retiredDismissalState,
                "retired inferred popup-dismissal state");
        }
        const std::string_view deferredQueue = ExtractSection(
            viewer,
            "static void QueueDeferredUiAction(",
            "static bool TryApplyDeferredDropdownUiActions(",
            "transition combo queue routing");
        RequireOrdered(
            deferredQueue,
            {
                "ImGuiID transitionComboId,",
                "state.transitionComboId = transitionComboId;",
                "state.transitionComboLastSubmittedFrame =",
                "transitionComboId != 0",
                "? ImGui::GetFrameCount()",
                ": -1;",
                "static void QueueDeferredControlUiAction(",
                "ImGui::GetItemID(),",
                "0,",
                "static void QueueDeferredDropdownUiAction(",
                "g_ActiveRoundedComboId,",
                "g_ActiveRoundedComboId,"
            },
            "ordinary controls have no popup transition while dropdown actions "
            "retain their exact originating combo");
        const std::string_view combo = ExtractSection(
            viewer,
            "static bool BeginRoundedCombo(",
            "template<typename Action>",
            "deferred dropdown trigger wrapper");
        Require(
            CountOccurrences(combo, "ImGui::BeginCombo(") == 1u,
            "the deferred dropdown trigger must use one native combo renderer");
        RequireContains(
            combo,
            "ImGui::BeginCombo(label, visiblePreview, flags);",
            "native integrated-arrow dropdown presentation");
        RequireOrdered(
            combo,
            {
                "if (open && deferredState.transitionComboId == comboId)",
                "deferredState.transitionComboLastSubmittedFrame =",
                "ImGui::GetFrameCount();"
            },
            "the exact open combo marks each frame that can advance its retained "
            "roll transition");
        for (const std::string_view retiredDropdownDrawing : {
                std::string_view("ImGuiComboFlags_NoArrowButton"),
                std::string_view("AddRectFilled"),
                std::string_view("ImGuiCol_Button"),
                std::string_view("DrawRoundedDownTriangle") })
        {
            RequireAbsent(
                combo,
                retiredDropdownDrawing,
                "deferred dropdown trigger manual arrow drawing");
        }
        RequireAbsent(
            viewer,
            "static void DrawRoundedDownTriangle(",
            "retired custom dropdown triangle helper");
        RequireAbsent(
            viewer,
            "static ImVec2 MovePointToward(",
            "retired custom dropdown triangle geometry helper");
        const std::string_view dropdown = ExtractSection(
            viewer,
            "static bool DrawDeferredDropdownOption(",
            "static void ApplyWordSpacing(",
            "deferred dropdown selection wrapper");
        RequireOrdered(
            dropdown,
            {
                "const bool activated = ImGui::Selectable(label, selected);",
                "if (!activated || selected)",
                "return false;",
                "QueueDeferredDropdownUiAction(",
                "return true;"
            },
            "native Selectable semantics with redundant-selection suppression and "
            "deferred mutation");
        const std::string_view deferredCommit = ExtractSection(
            viewer,
            "static bool TryApplyDeferredDropdownUiActions(",
            "static bool BeginRoundedCombo(",
            "deferred dropdown commit barrier");
        RequireOrdered(
            deferredCommit,
            {
                "UpdateUiDropdownIdleStartFrame(",
                "ShouldCommitDeferredDropdownActions(",
                "return false;",
                "std::move(state.actions)",
                "state = {};",
                "return actions.Drain("
            },
            "deferred dropdown idle-frame barrier and detached drain");
        const std::string_view deferredComposition = ExtractSection(
            viewer,
            "const auto deferredDropdownCompositionIdle =",
            "if (!m_ui.ShowUI && m_SettingsAppearance <= 0.f)",
            "deferred dropdown composition gate");
        RequireContains(
            deferredComposition,
            "!IsDeferredDropdownPopupTransitionActive()",
            "the exact popup roll transition participates in the complete composition "
            "barrier");
        const std::string_view hiddenDeferredCommit = ExtractSection(
            viewer,
            "if (!m_ui.ShowUI && m_SettingsAppearance <= 0.f)",
            "const float settingsAppearanceOpacity =",
            "hidden Settings deferred commit");
        RequireOrdered(
            hiddenDeferredCommit,
            {
                "FinishUnsubmittedDeferredDropdownPopupTransition();",
                "TryApplyDeferredDropdownUiActions(",
                "deferredDropdownCompositionIdle("
            },
            "hidden Settings finishes an unsubmitted exact combo before the "
            "ordinary deferred barrier");
        RequireAbsent(
            hiddenDeferredCommit,
            "!uiMotionEnabled",
            "skin-based immediate hidden-Settings dropdown commit");
        const std::string_view finalDeferredCommit = ExtractSection(
            viewer,
            "// Commit only after every UI window has finished composing.",
            "RestoreActiveUiWordSpacing();",
            "final deferred dropdown commit");
        RequireOrdered(
            finalDeferredCommit,
            {
                "FinishUnsubmittedDeferredDropdownPopupTransition();",
                "TryApplyDeferredDropdownUiActions(",
                "deferredDropdownCompositionIdle("
            },
            "visible Settings finishes an unsubmitted exact combo only after "
            "all root windows compose");
        RequireAbsent(
            finalDeferredCommit,
            "!uiMotionEnabled",
            "skin-based immediate visible dropdown commit");
        Require(
            CountOccurrences(
                viewer,
                "FinishUnsubmittedDeferredDropdownPopupTransition()") == 3u,
            "popup transition finisher must have one definition and exactly "
            "the hidden and visible composition calls.");
        const std::string_view pendingCommandGate = ExtractSection(
            viewer,
            "ImGui::Render();\n        if (m_PendingCommand &&",
            "if (pixelZoomPassActive && m_PixelZoomPass)",
            "slash-command deferred dropdown ordering");
        RequireOrdered(
            pendingCommandGate,
            {
                "if (m_PendingCommand &&",
                "!HasDeferredDropdownUiActions())",
                "finish its roll-up, settle, and full idle presentation",
                "UiCommand command = std::move(*m_PendingCommand);",
                "m_PendingCommand.reset();",
                "ExecuteUiCommand(command);"
            },
            "a newer slash command waits behind the complete dropdown roll, "
            "quarter-second settle, and idle-frame transaction");
        RequireAbsent(
            pendingCommandGate,
            "TryApplyDeferredDropdownUiActions(",
            "slash-command immediate deferred-action bypass");
        Require(
            CountOccurrences(
                viewer,
                "TryApplyDeferredDropdownUiActions(") == 3u,
            "the deferred commit helper must have one definition and only the "
            "hidden and visible ordinary barrier calls.");
        RequireContains(
            viewer,
            "CancelDeferredDropdownUiActions();",
            "scene-load stale selection cancellation");
        RequireOrdered(
            viewer,
            {
                "const bool deferredDropdownInputBlocked =",
                "if (deferredDropdownInputBlocked)",
                "ImGui::BeginDisabled();",
                "if (deferredDropdownInputBlocked)",
                "ImGui::EndDisabled();"
            },
            "deferred dropdown input safety");
        const std::string_view mutationLock = ExtractSection(
            viewer,
            "bool IsCommandRuntimeMutationLocked(",
            "static bool ApplyCommandBool(",
            "runtime command mutation lock");
        RequireContains(
            mutationLock,
            "definition.section != UiSettingsCommandSection::Ui",
            "loading-time UI command exception");
        RequireContains(
            mutationLock,
            "m_app->IsSceneBusy()",
            "scene-loading command mutation lock");
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: uvsr_ui_source_contract_tests <source-root>\n";
        return 2;
    }

    const std::filesystem::path root = argv[1];
    const std::string viewer = ReadFile(root / "src" / "uvsr.cpp");
    const std::string catalog = ReadFile(
        root / "src" / "ui_settings_command_catalog.h");
    const std::string temporalOptions = ReadFile(
        root / "src" / "temporal_aa_options.h");
    const std::string temporalPass = ReadFile(
        root / "src" / "temporal_aa.cpp");
    const std::string uiAnimation = ReadFile(
        root / "src" / "ui_animation.h");
    const std::string uiCommandLayout = ReadFile(
        root / "src" / "ui_command_layout.h");
    const std::string donutAppOverride = ReadFile(
        root / "overrides" / "donut-app.patch");
    const std::string donutAppUiPolishOverride = ReadFile(
        root / "overrides" / "donut-app-ui-polish.patch");
    const std::string imguiUiOverride = ReadFile(
        root / "overrides" / "imgui-ui.patch");
    const std::string imguiSliderOverride = ReadFile(
        root / "overrides" / "imgui-slider-controls.patch");
    const std::string imguiComboRollOverride = ReadFile(
        root / "overrides" / "imgui-combo-roll.patch");
    const std::string imguiUiPolishOverride = ReadFile(
        root / "overrides" / "imgui-ui-polish.patch");
    const std::string imguiTooltipPickerOverride = ReadFile(
        root / "overrides" / "imgui-tooltip-picker.patch");
    const std::string imguiUpstream = ReadFile(
        root / "donut" / "thirdparty" / "imgui" / "imgui.cpp");
    const std::string backdropBlurShader = ReadFile(
        root / "src" / "backdrop_blur_ps.hlsl");
    const std::string cmakeSource = ReadFile(root / "CMakeLists.txt");
    if (viewer.empty() || catalog.empty() || temporalOptions.empty() ||
        temporalPass.empty() || uiAnimation.empty() ||
        uiCommandLayout.empty() ||
        donutAppOverride.empty() || donutAppUiPolishOverride.empty() ||
        imguiUiOverride.empty() ||
        imguiSliderOverride.empty() || imguiComboRollOverride.empty() ||
        imguiUiPolishOverride.empty() ||
        imguiTooltipPickerOverride.empty() ||
        imguiUpstream.empty() ||
        backdropBlurShader.empty() ||
        cmakeSource.empty())
    {
        std::cerr << "FAIL: could not read current UI contract sources\n";
        return 2;
    }

    ValidateUiSpacing(viewer, uiCommandLayout);
    ValidateDrawers(viewer);
    ValidateRepresentation(viewer, catalog);
    ValidateNoise(viewer, catalog);
    ValidateVisibility(viewer, catalog);
    ValidateDenoising(viewer, catalog);
    ValidateBuffers(viewer);
    ValidatePerformance(viewer);
    ValidateTemporalTiming(temporalPass);
    ValidateAntiAliasing(viewer, temporalOptions);
    ValidateDebug(viewer);
    ValidateVisibilityPbrDecoupling(viewer);
    ValidateRayTracedShadows(viewer, catalog);
    ValidateCatalogAndDispatch(viewer, catalog);
    ValidateMaterialHistoryInvalidation(viewer);
    ValidateUiSafety(
        viewer,
        uiAnimation,
        donutAppOverride,
        donutAppUiPolishOverride,
        imguiUiOverride,
        imguiSliderOverride,
        imguiComboRollOverride,
        imguiUiPolishOverride,
        imguiTooltipPickerOverride,
        imguiUpstream,
        backdropBlurShader,
        cmakeSource);

    if (g_FailureCount != 0)
    {
        std::cerr << g_FailureCount << " UI source contract failure(s).\n";
        return 1;
    }
    return 0;
}
