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

    void ValidateDrawers(std::string_view viewer)
    {
        struct Drawer
        {
            std::string_view begin;
            std::string_view end;
            std::string_view body;
            std::string_view label;
        };
        const std::vector<Drawer> drawers = {
            {
                "const bool generalOpen = DrawCollapsingHeader(",
                "const bool indirectLightingOpen = DrawCollapsingHeader(",
                "##GeneralBody",
                "General"
            },
            {
                "const bool indirectLightingOpen = DrawCollapsingHeader(",
                "const bool buffersOpen = DrawCollapsingHeader(",
                "##VisibilityBody",
                "Visibility"
            },
            {
                "const bool buffersOpen = DrawCollapsingHeader(",
                "const bool statisticsOpen = DrawCollapsingHeader(",
                "##BuffersBody",
                "Buffers"
            },
            {
                "const bool statisticsOpen = DrawCollapsingHeader(",
                "const bool antiAliasingOpen = DrawCollapsingHeader(",
                "##StatisticsBody",
                "Statistics"
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
                "constexpr float ActionButtonCount = 4.f;",
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
    }

    void ValidateVisibility(
        std::string_view viewer,
        std::string_view catalog)
    {
        const std::string_view visibility = ExtractSection(
            viewer,
            "const bool indirectLightingOpen = DrawCollapsingHeader(",
            "const bool buffersOpen = DrawCollapsingHeader(",
            "Visibility drawer");
        const std::string_view noiseLabels = ExtractSection(
            visibility,
            "static constexpr const char* NoiseLabels[] = {",
            "};",
            "Visibility noise labels");
        RequireExactStrings(
            noiseLabels,
            {
                "Permutated White Noise",
                "Hashed White Noise",
                "Void Cluster Blue Noise"
            },
            "Visibility noise labels");
        RequireContains(
            visibility,
            "visibility.sampling.scheduler",
            "Visibility noise selector");
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
                "\"Ambient Occlusion##Visibility\"",
                "\"Indirect Diffuse##Visibility\"",
                "\"Sampling##Visibility\"",
                "\"Reconstruction##Visibility\""
            },
            "Visibility effect grouping");
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
                std::string_view("&visibility.indirectDiffuse.enabled"),
                std::string_view(
                    "visibility.sampling.maximumSampleCount"),
                std::string_view("visibility.reconstruction.mode"),
                std::string_view(
                    "&visibility.reconstruction.spatialEnabled") })
        {
            RequireContains(visibility, control, "Visibility core control");
        }
        RequireContains(
            Compact(catalog),
            "Value(\"visibility.noise\",Kind::Enum,Section::Visibility,"
            "\"permutated-white-noise|hashed-white-noise|"
            "void-cluster-blue-noise\")",
            "Visibility noise command domain");
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

    void ValidateBuffers(std::string_view viewer)
    {
        const std::string_view buffers = ExtractSection(
            viewer,
            "const bool buffersOpen = DrawCollapsingHeader(",
            "const bool statisticsOpen = DrawCollapsingHeader(",
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
                std::string_view("\"Ambient Occlusion\""),
                std::string_view("\"Indirect Diffuse\""),
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
    }

    void ValidateStatistics(std::string_view viewer)
    {
        const std::string_view statistics = ExtractSection(
            viewer,
            "const bool statisticsOpen = DrawCollapsingHeader(",
            "const bool antiAliasingOpen = DrawCollapsingHeader(",
            "Statistics drawer");
        RequireContains(
            statistics,
            "BuildPerformanceLine(m_PerformanceStatValues)",
            "dash-separated performance summary");
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
                "Screen-Space Visibility",
                "Screen-Space Shadows",
                "Temporal Reconstructive",
                "Conservative Morphological",
                "Multisample",
                "Material Picking",
                "Environment Background",
                "Tone Mapping",
                "Output Blit"
            },
            "Statistics effect labels");
        RequireContains(
            Compact(statistics),
            "static_assert(std::size(StatisticsEffectLabels)=="
            "static_cast<size_t>(StatisticsEffect::Count));",
            "Statistics selector enum coverage");
        RequireContains(
            statistics,
            "switch (selectedEffect)",
            "single-effect Statistics routing");
        RequireOrdered(
            statistics,
            {
                "ImGui::TextUnformatted(\"Effect\")",
                "DrawPresetResetIcon(",
                "\"##StatisticsEffect\"",
                "ImGui::SetItemDefaultFocus()"
            },
            "labeled Statistics selector and reset");
        RequireOrdered(
            statistics,
            {
                "\"Complete Renderer Frame\"",
                "\"Scene Setup and Clears\"",
                "\"Geometry\"",
                "\"Closest Surface Resolve\"",
                "\"Direct Lighting\"",
                "\"Screen-Space Visibility\"",
                "\"Material Picking\"",
                "\"Environment Background\"",
                "\"Tone Mapping\"",
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
                std::string_view("##VisibilityStatistics"),
                std::string_view("##ShadowStatistics"),
                std::string_view("##TemporalStatistics"),
                std::string_view("##MorphologicalStatistics"),
                std::string_view("##MultisampleStatistics") })
        {
            RequireContains(statistics, table, "detailed Statistics table");
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
            "m_RendererTimings.available[stageIndex] = true;",
            "completed renderer timing publication");
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
        for (const std::string_view retainedBreakdown : {
                std::string_view("First Trace"),
                std::string_view("Reconstruction"),
                std::string_view("Named-Stage Total"),
                std::string_view("Unattributed Timer Difference"),
                std::string_view("Sampling Resource Memory"),
                std::string_view("Work Groups"),
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
                "\"Temporal Cost\"",
                "ImGui::SetNextItemOpen(false, ImGuiCond_Once);",
                "\"Advanced##TemporalReconstructive\"",
                "ImGui::SeparatorText(\"Algorithm\")",
                "\"Previous-Depth Validation\"",
                "\"Conservative Morphological##Aliasing\"",
                "\"Enable##ConservativeMorphological\"",
                "\"Multisample Reference##Aliasing\"",
                "\"Enable##MultisampleReference\""
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
            "Previous-Depth Validation Quality ownership");
        RequireContains(
            aliasing,
            "!(aliasing.temporal.algorithmOverrides ==\n"
            "                    aliasingDefaults.temporal.algorithmOverrides)",
            "Algorithm override Quality ownership");
        RequireContains(
            aliasing,
            "const bool temporalCostCustom =",
            "Cost-owned Temporal Cost custom state");
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
                "Temporal Cost labels"),
            { "Full Quality", "Reduced", "Minimum" },
            "Temporal Cost labels");
        RequireContains(
            aliasing,
            "temporalQualityCustom,\n"
            "                applyTemporalQualityPreset);",
            "Quality custom preview and group binding");
        RequireContains(
            aliasing,
            "temporalCostCustom,\n"
            "                applyTemporalCostPreset);",
            "Temporal Cost custom preview and group binding");
        RequireContains(
            aliasing,
            "aliasingDefaults.temporal.quality ||\n"
            "                    temporalQualityCustom",
            "Quality group reset visibility");
        RequireContains(
            aliasing,
            "settings->temporal.stationaryBypass =\n"
            "                        stationaryBypass;",
            "Quality group Previous-Depth reset");
        RequireContains(
            aliasing,
            "settings->temporal.algorithmOverrides =\n"
            "                        algorithmOverrides;",
            "Quality group Algorithm reset");
        RequireContains(
            aliasing,
            "aliasingDefaults.temporal.costMode ||\n"
            "                    temporalCostCustom",
            "Temporal Cost group reset visibility");
        RequireContains(
            aliasing,
            "settings->temporal.behaviorOverrides =\n"
            "                        behaviorOverrides;",
            "Temporal Cost group policy reset");
        RequireContains(
            aliasing,
            "ui->TemporalAaSharpenEnabled = false;\n"
            "                    ui->TemporalAaSharpness =\n"
            "                        TemporalAaDefaultSharpness;",
            "Temporal Cost group sharpening reset");

        const std::string_view advanced = ExtractSection(
            aliasing,
            "\"Advanced##TemporalReconstructive\"",
            "\"Conservative Morphological##Aliasing\"",
            "Temporal Advanced section");
        for (const std::string_view control : {
                std::string_view("\"Previous-Depth Validation\""),
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
                "ImGui::SeparatorText(\"Algorithm\")",
                "\"Previous-Depth Validation\"",
                "\"Motion Source\"",
                "\"History Strength\"",
                "ImGui::SeparatorText(\"Cost\")",
                "\"History Storage\""
            },
            "Temporal Advanced section ordering");
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
            "\"History Frames\", &historyFrames, 1, 32",
            "intuitive history-frame range");
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
            CountOccurrences(aliasing, "BeginAnimatedTreeNode(") == 4u,
            "Aliasing must retain three technique disclosures and one "
            "Advanced disclosure.");

        const std::string_view temporalSettings = ExtractSection(
            temporalOptions,
            "struct TemporalAaSettings",
            "struct Cmaa2Settings",
            "Temporal AA defaults");
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
            Compact(cmaa2Settings),
            "boolenabled=false;",
            "CMAA2 default");
        RequireContains(
            Compact(msaaSettings),
            "boolenabled=false;",
            "MSAA default");
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
                "\"Physically Based Lighting##Debug\"",
                "\"Screen-Space Shadows##Debug\""
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
        RequireExactStrings(
            ExtractSection(
                debug,
                "static constexpr const char* IsolationLabels[] = {",
                "};",
                "shadow isolation labels"),
            { "Default", "Thread Lanes", "Wave Groups" },
            "shadow isolation labels");

        for (const std::string_view state : {
                std::string_view("m_ui.WhiteWorld"),
                std::string_view(
                    "m_ui.ScreenSpaceVisibility.debugView"),
                std::string_view("m_ui.LightingDebugView"),
                std::string_view("shadows.isolationView") })
        {
            RequireContains(debug, state, "composable Debug state");
        }
        Require(
            CountOccurrences(debug, "BeginAnimatedTreeNode(") == 4u,
            "Debug effects must retain four animated disclosures.");
        Require(
            CountOccurrences(debug, "ImGuiTreeNodeFlags_DefaultOpen") == 5u,
            "Debug and all four effect groups must start expanded.");
        RequireContains(
            debug,
            "can be combined with every effect-specific debug view.",
            "world and information-filter composability guidance");
        RequireContains(
            debug,
            "Visibility remains enabled and can still be inspected.",
            "lighting and Visibility composability guidance");
        RequireContains(
            viewer,
            "m_ScreenSpaceDirectionalShadowPass->PresentDebug(",
            "shadow debug presentation");
        for (const std::string_view removedOverlaySurface : {
                std::string_view("Edge Overlay"),
                std::string_view("Overlay Opacity"),
                std::string_view("edgeOverlay"),
                std::string_view("DebugOverlayOpacity") })
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
            "const bool indirectLightingOpen = DrawCollapsingHeader(",
            "const bool statisticsOpen = DrawCollapsingHeader(",
            "Visibility drawer");
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
            "constboolrunScreenSpaceVisibility="
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

    void ValidateScreenSpaceShadows(
        std::string_view viewer,
        std::string_view shadowSettings,
        std::string_view catalog)
    {
        const std::string_view shadows = ExtractSection(
            viewer,
            "const bool shadowsOpen = DrawCollapsingHeader(",
            "constexpr float ActionButtonCount = 4.f;",
            "Shadows drawer");
        RequireContains(
            shadows,
            "Screen-Space Directional Shadows##Shadows",
            "retained screen-space directional shadows");
        RequireContains(
            shadows,
            "m_app->HasPrimaryDirectionalLight()",
            "directional-light availability gate");
        for (const std::string_view control : {
                std::string_view("Enabled##ScreenSpaceShadows"),
                std::string_view("Profile##ScreenSpaceShadows"),
                std::string_view("Length##ScreenSpaceShadows"),
                std::string_view("Surface Thickness##ScreenSpaceShadows"),
                std::string_view("Bilinear Threshold##ScreenSpaceShadows"),
                std::string_view("Shadow Contrast##ScreenSpaceShadows"),
                std::string_view("Hard Shadow Samples##ScreenSpaceShadows"),
                std::string_view("Fade-Out Samples##ScreenSpaceShadows"),
                std::string_view("Ignore Edge Pixels##ScreenSpaceShadows"),
                std::string_view("Precision Offset##ScreenSpaceShadows"),
                std::string_view("Bilinear Offset Mode##ScreenSpaceShadows"),
                std::string_view("Early Out##ScreenSpaceShadows") })
        {
            RequireContains(shadows, control, "screen-space shadow control");
        }
        RequireAbsent(shadows, "\"Isolation View\"", "Shadows drawer");
        RequireAbsent(shadows, "\"Edge Overlay\"", "Shadows drawer");
        const std::string compactSettings = Compact(ExtractSection(
            shadowSettings,
            "struct ScreenSpaceDirectionalShadowSettings",
            "[[nodiscard]] inline constexpr uint32_t",
            "screen-space shadow defaults"));
        RequireContains(
            compactSettings,
            "boolenabled=false;",
            "screen-space shadows default");
        RequireContains(
            compactSettings,
            "ScreenSpaceShadowIsolationViewisolationView="
            "ScreenSpaceShadowIsolationView::None;",
            "shadow isolation default");
        RequireAbsent(
            compactSettings,
            "edgeOverlay",
            "retired shadow edge-overlay setting");

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
        const std::string_view catalog = ExtractSection(
            catalogSource,
            "inline constexpr auto UiSettingsCommandCatalog = std::array{",
            "inline constexpr std::array<std::string_view, 5>",
            "Settings command catalog");
        const std::vector<CatalogEntry> entries = ParseCatalog(catalog);
        Require(entries.size() == 122u,
            "Settings command catalog must contain exactly 122 entries.");

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
        Require(valueCount == 118u,
            "Settings command catalog must contain exactly 118 values.");
        Require(actions == std::set<std::string>{
                "open-scene-folder",
                "reset-settings",
                "restart",
                "screenshot"
            },
            "Settings command catalog must retain exactly four actions.");

        for (const std::string_view path : {
                std::string_view("visibility.noise"),
                std::string_view("anti-aliasing.taa.enabled"),
                std::string_view("anti-aliasing.taa.previous-depth"),
                std::string_view("anti-aliasing.cmaa2.enabled"),
                std::string_view("anti-aliasing.msaa.enabled"),
                std::string_view("debug.world.materials"),
                std::string_view("debug.visibility.view"),
                std::string_view("debug.pbr.filter"),
                std::string_view("debug.shadows.isolation"),
                std::string_view(
                    "shadows.screen-space-directional.enabled") })
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
                std::string_view("debug.visibility.indirect-diffuse-only"),
                std::string_view("visibility.ao.power"),
                std::string_view("buffers."),
                std::string_view("statistics.") })
        {
            for (const std::string& name : names)
            {
                Require(name.find(retiredPath) == std::string::npos,
                    "retired Settings command path returned: " + name);
            }
        }

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
                    "bool DispatchVisibilityCommandValue(",
                    "General command dispatcher")
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
                ExtractSection(viewer,
                    "bool DispatchAliasingCommandValue(",
                    "bool DispatchDebugCommandValue(",
                    "Aliasing command dispatcher")
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
                    "bool DispatchLightCommandValue(",
                    "Sky command dispatcher")
            },
            {
                "Lights",
                ExtractSection(viewer,
                    "struct FlashlightFloatCommandBinding",
                    "bool DispatchScreenSpaceShadowCommandValue(",
                    "Lights command dispatcher")
            },
            {
                "ScreenSpaceDirectionalShadows",
                ExtractSection(viewer,
                    "bool DispatchScreenSpaceShadowCommandValue(",
                    "bool DispatchMaterialCommandValue(",
                    "screen-space shadow command dispatcher")
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
                Require(
                    ContainsQuotedLiteral(dispatcher->second, entry.name),
                    "The " + entry.section + " dispatcher is missing " +
                        entry.name);
            }
        }

        const std::string_view router = ExtractSection(
            viewer,
            "bool DispatchCommandValue(",
            "bool DispatchCommandAction(",
            "Settings command section router");
        for (const std::string_view section : {
                std::string_view("Ui"),
                std::string_view("General"),
                std::string_view("Visibility"),
                std::string_view("Aliasing"),
                std::string_view("Debug"),
                std::string_view("Sky"),
                std::string_view("Lights"),
                std::string_view("ScreenSpaceDirectionalShadows"),
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

    void ValidateUiSafety(std::string_view viewer)
    {
        const std::string_view commandHeight = ExtractSection(
            viewer,
            "static float GetCommandInterfaceMinimumHeight()",
            "static constexpr float SettingsStatusLineSpacing",
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
        const std::string_view commandInterface = ExtractSection(
            viewer,
            "void DrawCommandInterface()",
            "void DrawMaterialInspector(",
            "one-row command interface");
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

        Require(
            CountOccurrences(viewer, "ImGui::Selectable(") == 1u,
            "all Settings dropdown choices must route through the one "
            "deferred selection wrapper.");
        const std::string_view dropdown = ExtractSection(
            viewer,
            "static bool DrawDeferredDropdownOption(",
            "static void ApplyWordSpacing(",
            "deferred dropdown selection wrapper");
        RequireContains(
            dropdown,
            "QueueDeferredDropdownUiAction(",
            "deferred dropdown selection wrapper");
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
    const std::string shadowSettings = ReadFile(
        root / "src" / "screen_space_directional_shadows_settings.h");
    if (viewer.empty() || catalog.empty() || temporalOptions.empty() ||
        temporalPass.empty() || shadowSettings.empty())
    {
        std::cerr << "FAIL: could not read current UI contract sources\n";
        return 2;
    }

    ValidateDrawers(viewer);
    ValidateVisibility(viewer, catalog);
    ValidateBuffers(viewer);
    ValidateStatistics(viewer);
    ValidateTemporalTiming(temporalPass);
    ValidateAntiAliasing(viewer, temporalOptions);
    ValidateDebug(viewer);
    ValidateVisibilityPbrDecoupling(viewer);
    ValidateScreenSpaceShadows(viewer, shadowSettings, catalog);
    ValidateCatalogAndDispatch(viewer, catalog);
    ValidateUiSafety(viewer);

    if (g_FailureCount != 0)
    {
        std::cerr << g_FailureCount << " UI source contract failure(s).\n";
        return 1;
    }
    return 0;
}
