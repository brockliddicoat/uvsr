#include "uvsr_ui_internal.h"

namespace
{
    struct UiSettingRange
    {
        float safeMinimum = 0.f;
        float safeMaximum = 0.f;
        float trackMinimum = 0.f;
        float trackMaximum = 0.f;
        float displayScale = 1.f;
    };

    [[nodiscard]] const UiSettingsCommandDefinition&
        GetUiSettingDefinition(SettingId id)
    {
        return *FindSettingsCommandDefinition(id);
    }

    [[nodiscard]] UiSettingRange GetUiSettingRange(
        SettingId id,
        bool useContextMaximum = false)
    {
        const UiSettingsCommandDefinition& definition =
            GetUiSettingDefinition(id);
        const UiSettingsTypedDomain& domain = definition.typedDomain;
        const UiSettingsPresentation& presentation =
            definition.presentation;
        const float displayScale = presentation.displayScale;
        const float trackMinimum = presentation.hasTrackRange
            ? static_cast<float>(presentation.trackMinimum)
            : static_cast<float>(domain.minimum);
        const float safeMaximum = useContextMaximum &&
            domain.hasContextMaximum
            ? static_cast<float>(domain.contextMaximum)
            : static_cast<float>(domain.maximum);
        const float trackMaximum = presentation.hasTrackRange
            ? static_cast<float>(presentation.trackMaximum)
            : safeMaximum;
        return {
            static_cast<float>(domain.minimum) * displayScale,
            safeMaximum * displayScale,
            trackMinimum * displayScale,
            trackMaximum * displayScale,
            displayScale
        };
    }

    [[nodiscard]] std::string GetUiSettingToken(
        SettingId id,
        std::size_t index)
    {
        const UiSettingsTypedDomain& domain =
            GetUiSettingDefinition(id).typedDomain;
        return index < domain.tokenCount
            ? std::string(domain.tokens[index])
            : std::string{};
    }

    [[nodiscard]] std::size_t GetUiSettingTokenIndex(
        SettingId id,
        std::string_view token)
    {
        const UiSettingsTypedDomain& domain =
            GetUiSettingDefinition(id).typedDomain;
        for (std::size_t index = 0u; index < domain.tokenCount; ++index)
        {
            if (domain.tokens[index] == token)
                return index;
        }
        assert(false && "The current setting token must belong to its typed domain");
        return 0u;
    }


}

void UIRenderer::ApplyUiStyle(float displayScale)
{
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = style.ChildRounding = style.PopupRounding = 4.f;
    style.FrameRounding = style.GrabRounding = 3.f;
    style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = style.FrameBorderSize = 0.f;
    const ImVec4 navy(33.f / 255.f, 49.f / 255.f, 75.f / 255.f, 1.f);
    const ImVec4 hover(44.f / 255.f, 71.f / 255.f, 116.f / 255.f, 1.f);
    const ImVec4 pressed(56.f / 255.f, 101.f / 255.f, 159.f / 255.f, 1.f);
    for (ImVec4& color : style.Colors)
        if (color.z > color.x && color.z > color.y)
            color.w = 1.f;
    for (ImGuiCol color : { ImGuiCol_FrameBg, ImGuiCol_Button, ImGuiCol_Header,
            ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed,
            ImGuiCol_Tab, ImGuiCol_TabDimmed })
        style.Colors[color] = navy;
    for (ImGuiCol color : { ImGuiCol_FrameBgHovered, ImGuiCol_ButtonHovered,
            ImGuiCol_HeaderHovered, ImGuiCol_TabHovered, ImGuiCol_TabSelected,
            ImGuiCol_TabDimmedSelected, ImGuiCol_SeparatorHovered })
        style.Colors[color] = hover;
    for (ImGuiCol color : { ImGuiCol_FrameBgActive, ImGuiCol_ButtonActive,
            ImGuiCol_HeaderActive, ImGuiCol_SeparatorActive })
        style.Colors[color] = pressed;
    // the window and drawer layers combine to 83.2 percent opacity.
    style.Colors[ImGuiCol_WindowBg].w = 0.72f;
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
    displayScale = std::clamp(displayScale, UiMinimumDisplayScale, UiMaximumDisplayScale);
    style.ScaleAllSizes(displayScale);
    g_UiSpacingTokens = ResolveUiSpacingTokens(displayScale);
    style.WindowPadding = ImVec2(g_UiSpacingTokens.regular, g_UiSpacingTokens.regular);
    style.ItemSpacing = ImVec2(g_UiSpacingTokens.regular, g_UiSpacingTokens.tight);
    style.ItemInnerSpacing = ImVec2(g_UiSpacingTokens.tight, g_UiSpacingTokens.tight);
    ImGui::GetStyle() = style;
}

UIRenderer::RootPanel UIRenderer::BeginRootPanel(
    bool performance, const ImVec2& position, float width, float maximumHeight)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float collapsedHeight = GetSettingsCollapsedWindowHeight(style, ImGui::GetFontSize());
    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    // Exact X constraints fix width while AlwaysAutoResize owns height.
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, performance ? collapsedHeight : 0.f),
        ImVec2(width, maximumHeight));
    ImGui::SetNextUvsrWindowCollapsedHeight(collapsedHeight);
    auto& request = performance ? m_PerformanceCollapsedRequest : m_SettingsCollapsedRequest;
    ImGui::SetNextWindowCollapsed(request.value_or(performance),
        request ? ImGuiCond_Always : ImGuiCond_Once);
    request.reset();
    ImGui::PushFont(m_UiHeaderFont->GetScaledFont());
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | (performance
        ? ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings
        : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const bool visible = ImGui::Begin(performance ? "Performance" : "Settings", nullptr, flags);
    const bool collapsed = ImGui::IsWindowCollapsed();
    ImGui::PopFont();
    return { ImGui::GetCurrentWindow(), visible && !collapsed, collapsed };
}

void UIRenderer::EndRootPanel()
{
    ImGui::End();
}

UIRenderer::RootPanelGeometry UIRenderer::GetRootPanelGeometry(const ImGuiWindow* window)
{
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    RootPanelGeometry result;
    result.body = ImRect(ImVec2(window->Pos.x, window->Pos.y + window->TitleBarHeight),
        ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y));
    result.content = ImRect(ImVec2(result.body.Min.x + padding.x, result.body.Min.y + padding.y),
        ImVec2(result.body.Max.x - padding.x, result.body.Max.y - padding.y));
    result.summary = ImRect(result.content.Min, ImVec2(result.content.Max.x,
        std::min(result.content.Max.y, result.content.Min.y + ImGui::GetFontSize() + g_UiSpacingTokens.tight)));
    return result;
}

auto UIRenderer::GetSettingsCollapsedWindowHeight(
        const ImGuiStyle& style,
        float fontSize) -> float {
        return (fontSize + style.FramePadding.y * 2.f) +
            style.WindowPadding.y * 2.f +
            fontSize +
            g_UiSpacingTokens.tight;
    }

bool UIRenderer::DrawCollapsingHeader(
    const char* label, const char* tooltip, ImGuiTreeNodeFlags flags)
{
    ImGui::PushFont(m_UiHeaderFont->GetScaledFont());
    ImGuiStyle& style = ImGui::GetStyle();
    const float itemSpacingY = std::exchange(style.ItemSpacing.y, 0.f);
    const bool open = ImGui::CollapsingHeader(label, flags);
    style.ItemSpacing.y = itemSpacingY;
    ImGui::PopFont();
    ImGui::SetItemTooltip("%s", tooltip);
    return open;
}

void UIRenderer::BeginDrawerBody(const char* id, float controlWidth, float maximumHeight)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(57.f / 255.f, 66.f / 255.f, 74.f / 255.f, 0.40f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(g_UiSpacingTokens.tight, g_UiSpacingTokens.tight));
    const bool scrollable = maximumHeight > 0.f;
    if (scrollable)
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.f, 0.f), ImVec2(FLT_MAX, maximumHeight));
    ImGui::BeginChild(id, ImVec2(0.f, 0.f),
        ImGuiChildFlags_AlwaysUseWindowPadding |
            ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize,
        scrollable ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushItemWidth(controlWidth);
}

auto UIRenderer::DrawCompactRootPanelBody(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& contentRect,
        float rounding,
        const char* text) -> ImRect {
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 textMinimum(
            contentRect.Min.x + g_UiSpacingTokens.tight,
            contentRect.Min.y);
        const ImRect textRect(
            textMinimum,
            ImVec2(
                textMinimum.x + textSize.x,
                textMinimum.y + textSize.y));
        drawList->PushClipRect(
            textMinimum,
            ImVec2(contentRect.Max.x, bodyRect.Max.y),
            true);
        drawList->AddText(
            textMinimum,
            ImGui::GetColorU32(ImGuiCol_Text),
            text);
        drawList->PopClipRect();
        return textRect;
    }

void UIRenderer::EndDrawerBody()
{
    ImGui::PopItemWidth();
    ImGuiStyle& style = ImGui::GetStyle();
    const float itemSpacingY = std::exchange(style.ItemSpacing.y, 0.f);
    ImGui::EndChild();
    style.ItemSpacing.y = itemSpacingY;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void UIRenderer::BeginControlRegion(ImGuiID id)
{
    const float width = ImGui::CalcItemWidth();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::BeginChild(id, ImVec2(0.f, 0.f),
        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushItemWidth(width);
}

void UIRenderer::EndControlRegion()
{
    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

bool UIRenderer::BeginSettingsTree(const char* label, ImGuiTreeNodeFlags flags, const char* tooltip)
{
    const ImGuiID id = ImGui::GetID(label);
    const bool open = ImGui::TreeNodeEx(label, flags | ImGuiTreeNodeFlags_NoTreePushOnOpen);
    if (tooltip)
        ImGui::SetItemTooltip("%s", tooltip);
    if (!open)
        return false;
    BeginControlRegion(id ^ ImGuiID(0xE60792B5u));
    const float indent = ImGui::GetStyle().IndentSpacing;
    ImGui::Indent(indent);
    g_NestedDrawerContexts.push_back({ ImGui::GetCurrentWindow(), indent });
    return true;
}

void UIRenderer::EndSettingsTree()
{
    assert(!g_NestedDrawerContexts.empty());
    ImGui::Unindent(g_NestedDrawerContexts.back().indentSpacing);
    g_NestedDrawerContexts.pop_back();
    ImGuiStyle& style = ImGui::GetStyle();
    const float itemSpacingY = std::exchange(style.ItemSpacing.y, 0.f);
    EndControlRegion();
    style.ItemSpacing.y = itemSpacingY;
}

bool UIRenderer::BeginToggleRegion(const char* id, bool visible)
{
    if (!visible)
        return false;
    BeginControlRegion(ImGui::GetID(id) ^ ImGuiID(0x6C3E91B7u));
    return true;
}

auto UIRenderer::DrawMaterialEditorTextureFilename(
        const char* filename,
        const float4& color) -> void {
        const std::string_view fullFilename =
            filename != nullptr ? std::string_view(filename) : std::string_view();
        const FrontEllipsisText formatted =
            FormatFrontEllipsisUtf8(fullFilename, 25u);
        ImGui::TextColored(
            ImVec4(color.x, color.y, color.z, color.w),
            "%s",
            formatted.display.c_str());
        if (formatted.truncated)
        {
            const FrontEllipsisText tooltip =
                FormatFrontEllipsisUtf8(fullFilename, 117u);
            ImGui::SetItemTooltip("%s", tooltip.display.c_str());
        }
    }

auto UIRenderer::SetNextLabeledControlWidth(
        const char* label,
        float preferredWidth) -> void {
        const ImGuiStyle& style = ImGui::GetStyle();
        const char* visibleLabelEnd =
            ImGui::FindRenderedTextEnd(label);
        const float visibleLabelWidth = visibleLabelEnd == label
            ? 0.f
            : ImGui::CalcTextSize(label, visibleLabelEnd).x +
                style.ItemInnerSpacing.x;
        const float resetLaneWidth =
            ImGui::GetFrameHeight() * 0.78f +
            style.ItemInnerSpacing.x;
        const float minimumControlWidth =
            ImGui::GetFrameHeight() * 3.f;
        const float maximumControlWidth = std::max(
            minimumControlWidth,
            ImGui::GetContentRegionAvail().x -
                visibleLabelWidth - resetLaneWidth);
        ImGui::SetNextItemWidth(std::min(
            preferredWidth,
            maximumControlWidth));
    }

auto UIRenderer::DrawPresetResetIcon(
        const char* id,
        bool modified,
        const char* tooltip,
        bool nestedDropdownGutterRequested) -> bool {
        ImGui::PushID(id);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonSize = ImGui::GetFrameHeight() * 0.78f;

        const bool nestedDropdownGutterAvailable =
            nestedDropdownGutterRequested &&
            !g_NestedDrawerContexts.empty() &&
            ImGui::GetCurrentWindow() ==
                g_NestedDrawerContexts.back().bodyWindow;
        if (nestedDropdownGutterRequested)
        {
            assert(nestedDropdownGutterAvailable);
        }
        if (nestedDropdownGutterAvailable)
        {
            const NestedDrawerContext& context =
                g_NestedDrawerContexts.back();
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            const float resetButtonScreenX =
                ImGui::GetCursorScreenPos().x +
                (-context.indentSpacing +
                    (context.indentSpacing - buttonSize) * 0.5f);
            const float sameLineOffset =
                resetButtonScreenX - window->Pos.x + window->Scroll.x -
                window->DC.GroupOffset.x - window->DC.ColumnsOffset.x;
            ImGui::SameLine(sameLineOffset, 0.f);
        }
        else
        {
            // Keep the established trailing lane unchanged for un-nested
            // dropdowns and every non-dropdown control.
            ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
            const float rightAlignedX =
                ImGui::GetContentRegionMax().x - buttonSize;
            if (ImGui::GetCursorPosX() < rightAlignedX)
                ImGui::SetCursorPosX(rightAlignedX);
        }

        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            modified ? style.Alpha : 0.f);
        ImGui::BeginDisabled(!modified);
        const bool pressed = ImGui::Button(
            "##PresetReset",
            ImVec2(buttonSize, buttonSize));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        const ImVec2 center(
            (minimum.x + maximum.x) * 0.5f,
            (minimum.y + maximum.y) * 0.5f);
        constexpr float Pi = 3.14159265358979323846f;
        const float radius = buttonSize * 0.24f;
        const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);
        drawList->PathClear();
        drawList->PathArcTo(
            center,
            radius,
            Pi * 0.12f,
            Pi * 1.72f,
            14);
        drawList->PathStroke(iconColor, false, 1.5f);
        const ImVec2 arrowTip(
            center.x + radius * std::cos(Pi * 0.12f),
            center.y + radius * std::sin(Pi * 0.12f));
        drawList->AddTriangleFilled(
            ImVec2(
                arrowTip.x + buttonSize * 0.01f,
                arrowTip.y - buttonSize * 0.16f),
            ImVec2(
                arrowTip.x + buttonSize * 0.16f,
                arrowTip.y + buttonSize * 0.01f),
            ImVec2(
                arrowTip.x - buttonSize * 0.05f,
                arrowTip.y + buttonSize * 0.04f),
            iconColor);
        if (modified)
            ImGui::SetItemTooltip("%s", tooltip);
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        ImGui::PopID();
        return pressed && modified;
    }

void UIRenderer::DrawPerformancePanelContents(
    float settingsControlWidth, const std::string& performanceLine)
{
    ImGui::PushItemWidth(settingsControlWidth);
    const float summaryCursorX = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(summaryCursorX + g_UiSpacingTokens.tight);
    ImGui::TextUnformatted(performanceLine.c_str());
    ImGui::SetItemTooltip("%s",
        "tris counts main-pass triangles after frustum culling; "
        "occluded, back-facing, or alpha-discarded ones may remain.");
    ImGui::SetCursorPosX(summaryCursorX);

    using Stage = RendererTimingStage;
    struct StatisticsView
    {
        const char* label;
        const char* table;
        const char* column;
        std::initializer_list<Stage> stages;
    };
    static const StatisticsView views[] = {
        { "Complete Renderer", "##CompleteRendererStatistics", "Graphics Stage", {
            Stage::CompleteFrame, Stage::SceneSetup, Stage::Geometry, Stage::PathTransport,
            Stage::ShadowRayDispatch, Stage::SkyVisibilityRayDispatch, Stage::DirectLighting,
            Stage::MaterialPicking, Stage::EnvironmentBackground, Stage::AutoExposure,
            Stage::ToneMapping, Stage::FastApproximate, Stage::OutputBlit } },
        { "Scene Setup", "##SelectedRendererStatistics", "Graphics Stage", { Stage::SceneSetup } },
        { "Geometry", "##SelectedRendererStatistics", "Graphics Stage", { Stage::Geometry } },
        { "Path Transport", "##SelectedRendererStatistics", "Graphics Stage", { Stage::PathTransport } },
        { "Direct Lighting", "##DirectLightingStatistics", "Lighting Stage", {
            Stage::DirectLighting } },
        { "Directional Shadows", "##ShadowStatistics", "Shadow Metric", {
            Stage::ShadowRayDispatch, Stage::SkyVisibilityRayDispatch } },
        { "Fast Approximate", "##SelectedRendererStatistics", "Graphics Stage", { Stage::FastApproximate } },
        { "Material Picking", "##SelectedRendererStatistics", "Graphics Stage", { Stage::MaterialPicking } },
        { "Environment Background", "##SelectedRendererStatistics", "Graphics Stage", { Stage::EnvironmentBackground } },
        { "Display Processing", "##ToneMappingStatistics", "Graphics Stage", { Stage::AutoExposure, Stage::ToneMapping } },
        { "Output Blit", "##SelectedRendererStatistics", "Graphics Stage", { Stage::OutputBlit } }
    };
    static_assert(std::size(views) == size_t(StatisticsEffect::Count));
    m_StatisticsEffect = std::clamp(m_StatisticsEffect, 0, int(std::size(views)) - 1);
    if (ImGui::BeginCombo("##StatisticsEffect", views[m_StatisticsEffect].label))
    {
        for (int index = 0; index < int(std::size(views)); ++index)
        {
            DrawDropdownOption(views[index].label, m_StatisticsEffect == index,
                [this, index]() { m_StatisticsEffect = index; });
            if (m_StatisticsEffect == index)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Choose GPU renderer timings. These exclude CPU work, UI rendering, presentation, and frame-limiter waits. The frame interval and FPS above include the whole loop. AgX Display Transform includes neutral HDR compression even when Tonemapper grading is off.");
    const auto& view = views[m_StatisticsEffect];
    const auto effect = StatisticsEffect(m_StatisticsEffect);
    const RendererTimings& timings = m_app->GetRendererTimings();
    ImGui::PushID(m_StatisticsEffect);
    if (ImGui::BeginTable(view.table, 2, ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn(view.column, ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthStretch, 1.35f);
        const auto beginRow = [](const char* label, bool available)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (available)
                ImGui::TextUnformatted(label);
            else
                ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
        };
        const auto milliseconds = [&](const char* label, double value, bool available)
        {
            const auto row = m_PerformanceTimingRows.Resolve(
                uint32_t(effect), ImHashStr(label), value, available);
            if (!row.IsVisible())
                return;
            beginRow(label, row.HasMeasurement());
            if (row.HasMeasurement())
                ImGui::Text("%.3f ms", row.milliseconds);
            else
                ImGui::TextDisabled("--");
        };
        const auto renderer = [&](Stage stage, bool eligible = true)
        {
            static constexpr std::pair<Stage, const char*> labels[] = {
                { Stage::CompleteFrame, "Complete Renderer Frame (GPU)" },
                { Stage::SceneSetup, "Scene Setup and Clears" },
                { Stage::Geometry, "Geometry" },
                { Stage::PathTransport, "Path Transport" },
                { Stage::ShadowRayDispatch, "Shadow Ray Dispatch" },
                { Stage::SkyVisibilityRayDispatch, "Sky Visibility Ray Dispatch" },
                { Stage::DirectLighting, "Direct Lighting" },
                { Stage::MaterialPicking, "Material Picking" },
                { Stage::EnvironmentBackground, "Environment Background" },
                { Stage::AutoExposure, "Auto Environment Exposure" },
                { Stage::ToneMapping, "AgX Display Transform" },
                { Stage::FastApproximate, "Fast Approximate" },
                { Stage::OutputBlit, "Output Blit" }
            };
            static_assert(std::size(labels) == size_t(Stage::Count));
            for (const auto& [candidate, label] : labels)
            {
                if (candidate != stage)
                    continue;
                {
                    const bool available = eligible && m_app->IsRendererStageActiveThisFrame(stage) &&
                        timings.IsAvailable(stage);
                    milliseconds(effect == StatisticsEffect::SceneSetup && stage == Stage::SceneSetup ? view.label : label,
                        available ? timings.Get(stage) : 0.0, available);
                }
                return;
            }
        };

        for (Stage stage : view.stages)
            renderer(stage);
        if (effect != StatisticsEffect::CompleteRenderer)
            renderer(Stage::CompleteFrame);
        ImGui::EndTable();
    }
    ImGui::PopID();
    ImGui::PopItemWidth();
}

UiSettingsValue UIRenderer::ReadUiPresentationValue(SettingId id)
{
    UiSettingsValue value;
    std::string error;
    // Disabled rows still display their retained values. Command reads keep
    // their availability checks; this read never requests a mutation.
    if (!DispatchTypedSetting(GetUiSettingDefinition(id), nullptr, value, error, true, true))
        uvsr::log::warning("UI setting read failed for %s: %s", SettingName(id).data(), error.c_str());
    return value;
}

bool UIRenderer::ApplyUiSetting(SettingId id, const UiSettingsValue& value)
{
    std::string error;
    const bool applied = ApplySettingValue(id, value, error);
    if (!applied)
        uvsr::log::warning("UI setting update failed: %s", error.c_str());
    return applied;
}

bool UIRenderer::ResetUiSetting(SettingId id)
{
    std::string error;
    const bool reset = ResetSettingValue(id, error);
    if (!reset)
        uvsr::log::warning("UI setting reset failed: %s", error.c_str());
    return reset;
}

bool UIRenderer::IsUiSettingChanged(SettingId id)
{
    const auto* definition = FindSettingsCommandDefinition(id);
    if (!definition || !definition->Supports(UiSettingsCommandVerb::Reset) ||
        !IsSettingAvailable(id))
        return false;
    std::string error;
    const bool atDefault = IsSettingAtContextualDefault(id, error);
    if (!error.empty())
        uvsr::log::warning("UI setting default check failed: %s", error.c_str());
    return error.empty() && !atDefault;
}

std::size_t UIRenderer::ReadUiTokenIndex(SettingId id)
{
    UiSettingsValue value;
    std::string error;
    if (ReadSettingValue(id, value, error) && value.kind == UiSettingsValueKind::Token)
        return GetUiSettingTokenIndex(id, value.text);
    uvsr::log::warning("UI setting read failed for %s: %s", SettingName(id).data(), error.c_str());
    return 0u;
}

bool UIRenderer::DrawUiReset(
    SettingId id, bool nested, const char* resetId, const char* tooltip)
{
    const bool changed = IsUiSettingChanged(id);
    const char* label = resetId ? resetId : SettingName(id).data();
    const bool pressed = DrawPresetResetIcon(label, changed, tooltip, nested);
    if (pressed)
        ResetUiSetting(id);
    return pressed;
}

void UIRenderer::DrawUiBoolean(
    const char* label, SettingId id, const char* tooltip,
    bool nestedReset, const std::string* texturePath, const char* resetId)
{
    bool current = ReadUiPresentationValue(id).boolean;
    if (ImGui::Checkbox(label, &current))
        ApplyUiSetting(id, UiSettingsValue::Boolean(current));
    ImGui::SetItemTooltip("%s", tooltip);
    if (texturePath)
    {
        ImGui::SameLine();
        const ImVec4 color = UiSuccessColor;
        DrawMaterialEditorTextureFilename(texturePath->c_str(), float4(color.x, color.y, color.z, color.w));
    }
    DrawUiReset(id, nestedReset, resetId);
}

void UIRenderer::DrawUiFloat(
    const char* label, SettingId id, const char* format,
    const char* tooltip, ImGuiSliderFlags flags, bool nestedReset,
    float width, bool useContextMaximum)
{
    const UiSettingRange range = GetUiSettingRange(id, useContextMaximum);
    float candidate = ReadUiPresentationValue(id).scalar * range.displayScale;
    if (width > 0.f)
        SetNextLabeledControlWidth(label, width);
    if (DrawBoundedSlider(label, &candidate,
            range.safeMinimum, range.safeMaximum, range.trackMinimum, range.trackMaximum,
            format, flags))
    {
        ApplyUiSetting(id, UiSettingsValue::Float(candidate / range.displayScale));
    }
    ImGui::SetItemTooltip("%s", tooltip);
    DrawUiReset(id, nestedReset);
}

void UIRenderer::DrawUiInteger(
    const char* label, SettingId id, const char* format,
    const char* tooltip, ImGuiSliderFlags flags, bool nestedReset)
{
    const UiSettingRange range = GetUiSettingRange(id);
    int current = static_cast<int>(ReadUiPresentationValue(id).integer);
    if (DrawBoundedSlider(label, &current,
            static_cast<int>(range.safeMinimum), static_cast<int>(range.safeMaximum),
            static_cast<int>(range.trackMinimum), static_cast<int>(range.trackMaximum), format, flags))
    {
        ApplyUiSetting(id, UiSettingsValue::Integer(current));
    }
    ImGui::SetItemTooltip("%s", tooltip);
    DrawUiReset(id, nestedReset);
}

void UIRenderer::DrawUiInheritedNumber(const char* label, SettingId id,
    const UiSettingsValue& inherited, const char* format, const char* tooltip)
{
    const auto& domain = GetUiSettingDefinition(id).typedDomain;
    const UiSettingRange range = GetUiSettingRange(id);
    assert(domain.hasAlternative);
    UiSettingsValue candidate = ReadUiPresentationValue(id);
    if (domain.kind == UiSettingsDomainKind::Integer)
    {
        int current = int(candidate.integer == int64_t(domain.alternative)
            ? inherited.integer : candidate.integer);
        if (DrawBoundedSlider(label, &current, int(range.safeMinimum), int(range.safeMaximum),
            int(range.trackMinimum), int(range.trackMaximum), format))
        {
            ApplyUiSetting(id, UiSettingsValue::Integer(current == inherited.integer
                ? int64_t(domain.alternative) : current));
        }
    }
    else
    {
        float current = (candidate.scalar == float(domain.alternative)
            ? inherited.scalar : candidate.scalar) * range.displayScale;
        if (DrawBoundedSlider(label, &current, range.safeMinimum, range.safeMaximum,
            range.trackMinimum, range.trackMaximum, format))
        {
            current /= range.displayScale;
            ApplyUiSetting(id, UiSettingsValue::Float(std::abs(current - inherited.scalar) < 1e-4f
                ? float(domain.alternative) : current));
        }
    }
    ImGui::SetItemTooltip("%s", tooltip);
    DrawUiReset(id);
}

void UIRenderer::DrawUiColor(
    const char* label, SettingId id, const char* tooltip, const char* resetId,
    float width, bool enabled)
{
    if (!enabled)
        return;
    UiSettingsValue value = UiSettingsValue::Vector({ 0.f, 0.f, 0.f, 0.f }, 3u);
    std::string error;
    if (IsSettingAvailable(id) && !ReadSettingValue(id, value, error))
        uvsr::log::warning("UI color read failed: %s", error.c_str());
    if (width > 0.f)
        SetNextLabeledControlWidth(label, width);
    if (ImGui::ColorEdit3(label, value.vector.data(),
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoTooltip))
    {
        const auto& domain = GetUiSettingDefinition(id).typedDomain;
        assert(domain.hasRange && domain.kind == UiSettingsDomainKind::Float3);
        for (std::size_t index = 0u; index < 3u; ++index)
            value.vector[index] = std::clamp(value.vector[index],
                static_cast<float>(domain.minimum), static_cast<float>(domain.maximum));
        ApplyUiSetting(id, value);
    }
    ImGui::SetItemTooltip("%s", tooltip);
    DrawUiReset(id, false, resetId);

}

void UIRenderer::DrawUiTokenOption(SettingId id, std::size_t index, bool selected)
{
    const std::string label = FormatUiSettingsTokenLabel(id, index);
    DrawDropdownOption(label.c_str(), selected, [this, id, index]()
    {
        ApplyUiSetting(id, UiSettingsValue::Token(GetUiSettingToken(id, index)));
    });
}

void UIRenderer::DrawUiRoundedTokenCombo(
    const char* label, SettingId id, bool custom, bool focusSelected)
{
    const auto& domain = GetUiSettingDefinition(id).typedDomain;
    const std::size_t current = ReadUiTokenIndex(id);
    std::string preview = FormatUiSettingsTokenLabel(id, current);
    if (custom)
        preview += " (Custom)";
    if (!ImGui::BeginCombo(label, preview.c_str()))
        return;
    for (std::size_t index = 0u; index < domain.tokenCount; ++index)
    {
        DrawUiTokenOption(id, index, !custom && index == current);
        if (focusSelected && index == current)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
}

void UIRenderer::DrawUiTokenCombo(
    const char* label, SettingId id, const char* tooltip, bool nestedReset)
{
    const auto& domain = GetUiSettingDefinition(id).typedDomain;
    std::array<std::string, UiSettingsTypedDomain::MaximumTokenCount> labels;
    std::array<const char*, UiSettingsTypedDomain::MaximumTokenCount> pointers{};
    for (std::size_t index = 0u; index < domain.tokenCount; ++index)
    {
        labels[index] = FormatUiSettingsTokenLabel(id, index);
        pointers[index] = labels[index].c_str();
    }
    int candidate = static_cast<int>(ReadUiTokenIndex(id));
    if (ImGui::Combo(label, &candidate, pointers.data(), domain.tokenCount))
        ApplyUiSetting(id, UiSettingsValue::Token(GetUiSettingToken(id, std::size_t(candidate))));
    ImGui::SetItemTooltip("%s", tooltip);
    DrawUiReset(id, nestedReset);
}

auto UIRenderer::DrawGeneralDrawer(float settingsControlWidth) -> void {
        const bool generalOpen = DrawCollapsingHeader(
            "General",
            "Show general renderer settings.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (!generalOpen)
        {
            ImGui::Spacing();
            return;
        }

        BeginDrawerBody("##GeneralBody", settingsControlWidth);

        ImGui::TextUnformatted("Lighting Solution");
        ImGui::SetNextItemWidth(-FLT_MIN);
        DrawUiRoundedTokenCombo("##LightingSolution", SettingId::LightingSolution);
        ImGui::SetItemTooltip(
            "Choose the scene-lighting pipeline. Ray Tracing uses deferred "
            "lighting with selective ray-traced visibility. Path Tracing "
            "integrates light transport through the shared scene.");

        if (!m_ui.GpuAdapterChoices.empty())
        {
            const GpuAdapterChoice* activeAdapter =
                GetActiveGpuAdapterChoice();
            const char* activeAdapterName = activeAdapter
                ? activeAdapter->name.c_str()
                : "Unknown adapter";

            ImGui::TextUnformatted("Graphics Adapter");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo(
                    "##GraphicsAdapter",
                    activeAdapterName))
            {
                for (const GpuAdapterChoice& adapter :
                    m_ui.GpuAdapterChoices)
                {
                    const bool selected =
                        adapter.adapterIndex ==
                        m_ui.ActiveGpuAdapterIndex;
                    DrawDropdownOption(
                        adapter.name.c_str(),
                        selected,
                        [this, adapterIndex = adapter.adapterIndex]()
                        {
                            std::string error;
                            if (!ApplySettingValue(
                                    SettingId::GpuAdapter,
                                    UiSettingsValue::Selector(
                                        FormatSettingsSnapshotAdapterToken(
                                            adapterIndex)),
                                    error))
                            {
                                uvsr::log::warning(
                                    "Graphics adapter update failed: %s",
                                    error.c_str());
                            }
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the graphics processor. UVSR restarts after a "
                "change.");
        }

        ImGui::TextUnformatted("Camera Mode");
        DrawUiReset(SettingId::CameraMode, false, "Camera Mode");
        ImGui::SetNextItemWidth(-FLT_MIN);
        DrawUiRoundedTokenCombo("##Camera", SettingId::CameraMode);
        ImGui::SetItemTooltip(
            "Choose Freelook or Locked. Q moves up, E moves down, X/C roll, "
            "and V levels the roll.");

        const ImGuiStyle& style = ImGui::GetStyle();
        const std::string currentScene =
            m_app->GetCurrentSceneName();
        const std::string currentSceneDisplayName =
            m_app->GetCurrentSceneDisplayName();
        const float folderButtonWidth = ImGui::GetFrameHeight();
        ImGui::TextUnformatted("World Scenes");
        ImGui::SetNextItemWidth(
            -(folderButtonWidth + style.ItemSpacing.x));
        if (ImGui::BeginCombo(
                "##Scene",
                currentSceneDisplayName.c_str()))
        {
            const std::vector<SceneCatalogEntry>& scenes =
                m_app->GetAvailableScenes();
            for (const SceneCatalogEntry& scene : scenes)
            {
                ImGui::PushID(scene.FileName.c_str());
                const bool selected =
                    scene.FileName == currentScene;
                DrawDropdownOption(
                    scene.DisplayName.c_str(),
                    selected,
                    [this,
                        sceneToken = FormatSettingsSnapshotSceneToken(
                            MakeSceneDisplayName(m_app->GetSceneDir(), scene.FileName))]()
                    {
                        ApplyUiSetting(
                            SettingId::SceneCurrent,
                            UiSettingsValue::Selector(sceneToken));
                    });
                if (selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Load a different scene.");

        ImGui::SameLine();
        const bool openSceneFolderPressed = ImGui::Button(
            "##OpenSceneFolder",
            ImVec2(folderButtonWidth, ImGui::GetFrameHeight()));
        const ImVec2 iconMinimum = ImGui::GetItemRectMin();
        const ImVec2 iconMaximum = ImGui::GetItemRectMax();
        if (openSceneFolderPressed)
        {
            std::string error;
            if (!RunAction(ActionId::OpenSceneFolder, error))
            {
                uvsr::log::warning(
                    "Open scene folder failed: %s",
                    error.c_str());
            }
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float iconWidth = iconMaximum.x - iconMinimum.x;
        const float iconHeight = iconMaximum.y - iconMinimum.y;
        const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);
        const ImVec2 folderBodyMinimum(
            iconMinimum.x + iconWidth * 0.20f,
            iconMinimum.y + iconHeight * 0.38f);
        const ImVec2 folderBodyMaximum(
            iconMaximum.x - iconWidth * 0.20f,
            iconMaximum.y - iconHeight * 0.22f);
        drawList->AddRect(
            folderBodyMinimum,
            folderBodyMaximum,
            iconColor,
            1.5f,
            0,
            1.5f);
        drawList->AddLine(
            folderBodyMinimum,
            ImVec2(
                folderBodyMinimum.x + iconWidth * 0.22f,
                iconMinimum.y + iconHeight * 0.27f),
            iconColor,
            1.5f);
        drawList->AddLine(
            ImVec2(
                folderBodyMinimum.x + iconWidth * 0.22f,
                iconMinimum.y + iconHeight * 0.27f),
            ImVec2(
                folderBodyMinimum.x + iconWidth * 0.40f,
                folderBodyMinimum.y),
            iconColor,
            1.5f);
        ImGui::SetItemTooltip("Open the scene folder.");

        if (m_SceneLoadFailed || m_app->HasSceneLoadFailure())
        {
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                UiErrorColor);
            const std::string& workerFailure =
                m_app->GetSceneLoadFailure();
            if (workerFailure.empty())
            {
                ImGui::TextWrapped(
                    "The selected scene could not be loaded.");
            }
            else
            {
                ImGui::TextWrapped(
                    "The selected scene could not be loaded: %s",
                    workerFailure.c_str());
            }
            ImGui::PopStyleColor();
            if (ImGui::Button(
                    "Retry Scene Load",
                    ImVec2(-FLT_MIN, 0.f)))
            {
                m_SceneLoadFailed = false;
                m_app->RetryCurrentSceneLoad();
            }
            ImGui::SetItemTooltip(
                "Retry loading the currently selected scene.");
        }

        EndDrawerBody();
        ImGui::Spacing();
    }

auto UIRenderer::DrawPathingDrawer(float settingsControlWidth) -> void {
        if (std::exchange(m_PathingDrawerOpenRequested, false))
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (!DrawCollapsingHeader(
                "Pathing",
                "Control path length and bright outlier suppression."))
        {
            return;
        }

        BeginDrawerBody("##PathingBody", settingsControlWidth);
        DrawUiInteger("Max Bounces", SettingId::PathingMaximumBounces, "%d",
            "Maximum scattering bounces, 1 to 30. The camera surface is depth zero.");
        int minimumBounces = m_ui.PathTracing.minimumBounces;
        if (DrawBoundedSlider("Min Bounces", &minimumBounces,
                1, m_ui.PathTracing.maximumBounces, 1, m_ui.PathTracing.maximumBounces))
            ApplyUiSetting(SettingId::PathingMinimumBounces, UiSettingsValue::Integer(minimumBounces));
        ImGui::SetItemTooltip(
            "Delays Russian roulette. Following Capsaicin, roulette begins after a surface depth greater than this value. "
            "A path can still end at an emitter, empty space, or the maximum bounce limit.");
        DrawUiReset(SettingId::PathingMinimumBounces);
        DrawUiBoolean("Firefly Filter", SettingId::PathingFireflyFilter,
            "Limits rare bright contributions before accumulation. This biased filter can darken intense light paths.");
        ImGui::BeginDisabled(!m_ui.PathTracing.fireflyFilter);
        DrawUiFloat("Firefly Threshold", SettingId::PathingFireflyThreshold, "%.0f",
            "Maximum average RGB radiance per event, reduced by path scattering probability. "
            "Lower values suppress more bright outliers. Independent of exposure.", ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();
        const WorldSpaceRepresentationStatus& representation =
            m_app->GetWorldSpaceRepresentationStatus();
        const PathTracingCapabilities& capabilities =
            m_app->GetPathTracingCapabilities();
        const PathTracingSceneDomainStatus sceneDomain =
            m_app->GetPathTracingSceneDomainStatus();
        if (!m_ui.Representation.allowRayTraversal)
        {
            ImGui::TextDisabled(
                "Enable Allow Ray Traversal in Developer > Advanced.");
        }
        else if (sceneDomain == PathTracingSceneDomainStatus::Unsupported)
        {
            ImGui::TextDisabled(
                "Path Tracing is unavailable for this scene's geometry or "
                "materials. It remains selected; no raster transport is "
                "substituted.");
        }
        else if (!capabilities.rayQuerySupported ||
            representation.state == WorldSpaceRepresentationState::Unsupported ||
            representation.state == WorldSpaceRepresentationState::Failed)
        {
            ImGui::TextDisabled(
                "DXR 1.1 inline ray queries and a valid world hierarchy are required.");
        }
        else if (representation.state == WorldSpaceRepresentationState::BuildingBlas ||
            representation.state == WorldSpaceRepresentationState::BuildingTlas)
        {
            ImGui::TextDisabled(
                "Preparing path transport: BLAS %u/%u.",
                representation.builtBlasCount,
                representation.totalBlasCount);
        }
        else if (m_app->GetSelectedLightingTransportState() ==
            SelectedLightingTransportState::PathTracingUnavailable)
        {
            ImGui::TextDisabled(
                "Path transport failed or is unavailable. Path Tracing "
                "remains selected and no raster transport is rendered.");
        }
        else if (m_app->GetSelectedLightingTransportState() ==
            SelectedLightingTransportState::PathTracingPreparing)
        {
            ImGui::TextDisabled("Preparing path transport.");
        }
        else
        {
            if (sceneDomain ==
                PathTracingSceneDomainStatus::BlendedGeometryOmitted)
            {
                ImGui::TextDisabled(
                    "Alpha-blended geometry is omitted; opaque and alpha-tested geometry is traced.");
            }
            const uint64_t acceptedSampleCount =
                m_app->GetPathTracingCenterPixelAcceptedSampleCount();
            ImGui::Text(
                "Center-pixel accepted history: %llu sample%s",
                static_cast<unsigned long long>(acceptedSampleCount),
                acceptedSampleCount == 1u ? "" : "s");
            ImGui::SetItemTooltip(
                "Resets on camera, scene, resize, renderer-setting, and "
                "explicit history invalidation. This asynchronous GPU "
                "readback reports successful accumulated samples at the "
                "viewport center, not CPU dispatch attempts.");
        }
        EndDrawerBody();
    }

auto UIRenderer::DrawMaterialDrawer(float settingsControlWidth) -> void {
        ImGui::SetNextItemOpen(
            m_ui.ShowMaterialDrawer,
            ImGuiCond_Always);
        const bool materialBodyVisible = DrawCollapsingHeader(
            "Material",
            "Inspect and edit the surface under the center crosshair. Press M "
            "to refresh the center selection.",
            ImGuiTreeNodeFlags_None);
        const bool targetOpen = materialBodyVisible;
        if (targetOpen != m_ui.ShowMaterialDrawer)
        {
            ApplyUiSetting(
                SettingId::MaterialEditorVisible,
                UiSettingsValue::Boolean(targetOpen));
        }
        if (m_MaterialRevealRequested && targetOpen)
        {
            ImGui::SetScrollHereY(0.f);
            m_MaterialRevealRequested = false;
        }

        if (!materialBodyVisible)
        {
            ImGui::Spacing();
            return;
        }

        BeginDrawerBody("##MaterialBody", settingsControlWidth);
        auto material = m_ui.SelectedMaterial;
        if (material)
        {
            const std::string materialPrefix = "Material " + std::to_string(material->materialID) + ":";
            ImGui::BeginGroup();
            ImGui::TextUnformatted(materialPrefix.c_str());
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            const float nameWidth = ImGui::GetContentRegionAvail().x;
            const bool truncated = ImGui::CalcTextSize(material->name.c_str()).x > nameWidth;
            std::string name = material->name;
            if (truncated)
            {
                const char* end = name.data();
                ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(),
                    std::max(0.f, nameWidth - ImGui::CalcTextSize("...").x), 0.f,
                    name.data(), name.data() + name.size(), &end);
                name.resize(size_t(end - name.data()));
                name += "...";
            }
            ImGui::TextColored(UiSuccessColor, "%s", name.c_str());
            ImGui::EndGroup();
            if (truncated)
                ImGui::SetItemTooltip("%s", material->name.c_str());

            ImGui::PushID(material->materialID);

            SetNextLabeledControlWidth("Material Domain", settingsControlWidth);
            DrawUiRoundedTokenCombo(
                "Material Domain##MaterialDomain", SettingId::MaterialSelectedDomain, false, false);
            ImGui::SetItemTooltip(
                "Choose how the selected surface is rendered.");
            DrawUiReset(SettingId::MaterialSelectedDomain, false, "Material Domain");

            DrawUiBoolean(
                "Double-Sided",
                SettingId::MaterialSelectedDoubleSided,
                "Render both sides of the selected surface.");

            if (IsSettingAvailable(
                    SettingId::MaterialSelectedBaseTextureEnabled))
            {
                DrawUiBoolean(
                    material->useSpecularGlossModel
                        ? "Use Diffuse Texture"
                        : "Use Base Color Texture",
                    SettingId::MaterialSelectedBaseTextureEnabled,
                    "Enable the selected material's base or diffuse texture.",
                    false,
                    &material->baseOrDiffuseTexture->path);
            }
            DrawUiColor(
                material->useSpecularGlossModel
                    ? material->enableBaseOrDiffuseTexture
                        ? "Diffuse Factor"
                        : "Diffuse Color"
                    : material->enableBaseOrDiffuseTexture
                        ? "Base Color Factor"
                        : "Base Color",
                SettingId::MaterialSelectedBaseColor,
                "Set the selected material's base or diffuse color.",
                nullptr,
                settingsControlWidth);

            if (IsSettingAvailable(
                    SettingId::MaterialSelectedMetalSpecularTextureEnabled))
            {
                DrawUiBoolean(
                    material->useSpecularGlossModel
                        ? "Use Specular Texture"
                        : "Use Metal-Rough Texture",
                    SettingId::MaterialSelectedMetalSpecularTextureEnabled,
                    "Enable the selected material's metal rough or specular texture.",
                    false,
                    &material->metalRoughOrSpecularTexture->path);
            }

            if (material->useSpecularGlossModel)
            {
                DrawUiColor(
                    material->enableMetalRoughOrSpecularTexture
                        ? "Specular Factor"
                        : "Specular Color",
                    SettingId::MaterialSelectedSpecularColor,
                    "Set the selected material's specular color.",
                    nullptr,
                    settingsControlWidth);
                const UiSettingRange roughnessRange =
                    GetUiSettingRange(
                        SettingId::MaterialSelectedRoughness);
                float glossiness = 1.f - material->roughness;
                SetNextLabeledControlWidth(
                    "Glossiness",
                    settingsControlWidth);
                if (DrawBoundedSlider(
                        material->enableMetalRoughOrSpecularTexture
                            ? "Glossiness Factor"
                            : "Glossiness",
                        &glossiness,
                        roughnessRange.safeMinimum,
                        roughnessRange.safeMaximum,
                        roughnessRange.trackMinimum,
                        roughnessRange.trackMaximum,
                        "%.3f"))
                {
                    ApplyUiSetting(
                        SettingId::MaterialSelectedRoughness,
                        UiSettingsValue::Float(1.f - glossiness));
                }
                ImGui::SetItemTooltip(
                    "Set glossiness as one minus the canonical roughness value.");
                DrawUiReset(
                    SettingId::MaterialSelectedRoughness);
            }
            else
            {
                DrawUiFloat(
                    material->enableMetalRoughOrSpecularTexture
                        ? "Metalness Factor"
                        : "Metalness",
                    SettingId::MaterialSelectedMetalness,
                    "%.3f",
                    "Set the selected material's metalness.",
                    ImGuiSliderFlags_None,
                    false,
                    settingsControlWidth,
                    false);
                DrawUiFloat(
                    material->enableMetalRoughOrSpecularTexture
                        ? "Roughness Factor"
                        : "Roughness",
                    SettingId::MaterialSelectedRoughness,
                    "%.3f",
                    "Set the selected material's roughness.",
                    ImGuiSliderFlags_None,
                    false,
                    settingsControlWidth,
                    false);
            }

            if (IsSettingAvailable(
                    SettingId::MaterialSelectedOpacity))
            {
                DrawUiFloat(
                    material->baseOrDiffuseTexture
                        ? "Opacity Factor"
                        : "Opacity",
                    SettingId::MaterialSelectedOpacity,
                    "%.3f",
                    "Set opacity for the alpha blended material.",
                    ImGuiSliderFlags_None,
                    false,
                    settingsControlWidth,
                    static_cast<bool>(material->baseOrDiffuseTexture));
            }
            if (IsSettingAvailable(
                    SettingId::MaterialSelectedAlphaCutoff))
            {
                DrawUiFloat(
                    "Alpha Cutoff",
                    SettingId::MaterialSelectedAlphaCutoff,
                    "%.3f",
                    "Set the alpha test cutoff.",
                    ImGuiSliderFlags_None,
                    false,
                    settingsControlWidth,
                    false);
            }

            if (IsSettingAvailable(
                    SettingId::MaterialSelectedNormalTextureEnabled))
            {
                DrawUiBoolean(
                    "Use Normal Texture",
                    SettingId::MaterialSelectedNormalTextureEnabled,
                    "Enable the selected material's normal texture.",
                    false,
                    &material->normalTexture->path);
                if (material->enableNormalTexture)
                {
                    DrawUiFloat(
                        "Normal Scale",
                        SettingId::MaterialSelectedNormalScale,
                        "%.3f",
                        "Set the normal texture scale.",
                        ImGuiSliderFlags_None,
                        false,
                        settingsControlWidth,
                        false);
                }
            }

            if (IsSettingAvailable(
                    SettingId::MaterialSelectedOcclusionTextureEnabled))
            {
                DrawUiBoolean(
                    "Use Occlusion Texture",
                    SettingId::MaterialSelectedOcclusionTextureEnabled,
                    "Enable the selected material's occlusion texture.",
                    false,
                    &material->occlusionTexture->path);
                if (material->enableOcclusionTexture)
                {
                    DrawUiFloat(
                        "Occlusion Strength",
                        SettingId::MaterialSelectedOcclusionStrength,
                        "%.3f",
                        "Set the occlusion texture strength.",
                        ImGuiSliderFlags_None,
                        false,
                        settingsControlWidth,
                        false);
                }
            }

            if (IsSettingAvailable(
                    SettingId::MaterialSelectedEmissiveTextureEnabled))
            {
                DrawUiBoolean(
                    "Use Emissive Texture",
                    SettingId::MaterialSelectedEmissiveTextureEnabled,
                    "Enable the selected material's emissive texture.",
                    false,
                    &material->emissiveTexture->path);
            }
            DrawUiColor(
                "Emissive Color",
                SettingId::MaterialSelectedEmissiveColor,
                "Set the selected material's emissive color.",
                nullptr,
                settingsControlWidth);
            DrawUiFloat(
                "Emissive Intensity",
                SettingId::MaterialSelectedEmissiveIntensity,
                "%.3f",
                "Set the selected material's emissive intensity.",
                ImGuiSliderFlags_Logarithmic,
                false,
                settingsControlWidth,
                false);

            if (IsSettingAvailable(
                    SettingId::MaterialSelectedTransmissionFactor))
            {
                if (IsSettingAvailable(
                        SettingId::MaterialSelectedTransmissionTextureEnabled))
                {
                    DrawUiBoolean(
                        "Use Transmission Texture",
                        SettingId::MaterialSelectedTransmissionTextureEnabled,
                        "Enable the selected material's transmission texture.",
                        false,
                        &material->transmissionTexture->path);
                }
                DrawUiFloat(
                    "Transmission Factor",
                    SettingId::MaterialSelectedTransmissionFactor,
                    "%.3f",
                    "Set the selected material's transmission factor.",
                    ImGuiSliderFlags_None,
                    false,
                    settingsControlWidth,
                    false);
            }

            if (IsSettingAvailable(
                    SettingId::MaterialSelectedAlphaMaskTextureEnabled))
            {
                DrawUiBoolean(
                    "Use Alpha Mask Texture",
                    SettingId::MaterialSelectedAlphaMaskTextureEnabled,
                    "Enable the selected material's alpha mask texture.",
                    false,
                    &material->opacityTexture->path);
            }
            ImGui::PopID();
        }
        else
        {
            ImGui::TextWrapped(
                "Aim the center crosshair at an editable surface, then press "
                "M.");
        }
        EndDrawerBody();
        ImGui::Spacing();
    }

void UIRenderer::UpdateStatSnapshot(int width, int height)
{
    constexpr double interval = 1.0 / 24.0;
    const double frameTime = std::max(0.0, double(ImGui::GetIO().DeltaTime));
    m_StatSnapshotElapsed += frameTime;
    if (frameTime > 0.0)
    {
        m_StatFrameTimeSum += frameTime;
        ++m_StatFrameTimeCount;
    }
    if (m_HasAppliedStatSnapshot && m_StatSnapshotElapsed < interval)
        return;

    m_StatSnapshotElapsed = m_HasAppliedStatSnapshot
        ? std::fmod(m_StatSnapshotElapsed, interval) : 0.0;
    if (m_StatFrameTimeCount > 0u)
        m_DisplayedFrameTime = m_StatFrameTimeSum / double(m_StatFrameTimeCount);
    m_StatFrameTimeSum = 0.0;
    m_StatFrameTimeCount = 0u;
    m_HasAppliedStatSnapshot = true;
    FormatStatLine(m_PerformanceStatValues[0], "%d x %d", width, height);
    m_PerformanceStatValues[3] = FormatTriangleCount(m_app->GetSubmittedMainViewTriangles());
    if (m_DisplayedFrameTime > 0.0)
    {
        FormatStatLine(m_PerformanceStatValues[1], "%.1f ms", m_DisplayedFrameTime * 1e3);
        FormatStatLine(m_PerformanceStatValues[2], "%.1f fps", 1.0 / m_DisplayedFrameTime);
    }
    else
    {
        m_PerformanceStatValues[1].clear();
        m_PerformanceStatValues[2].clear();
    }

}

auto UIRenderer::DrawCenteredActionButton(const char* label, float width) -> bool {
        const ImVec2 size(width, ImGui::GetFrameHeight());
        ImGui::PushID(label);
        const bool pressed = ImGui::Button("##ActionButton", size);
        ImGui::PopID();
        const ImVec2 buttonMin = ImGui::GetItemRectMin();
        const ImVec2 buttonMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 textPosition(
            std::floor(buttonMin.x + (buttonMax.x - buttonMin.x - textSize.x) * 0.5f),
            std::floor(buttonMin.y + (buttonMax.y - buttonMin.y - textSize.y) * 0.5f +
                       1.f));
        drawList->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text), label);
        return pressed;
    }

auto UIRenderer::buildUI(void) -> void {
        const auto runUiAction = [this](ActionId id)
        {
            std::string error;
            if (!RunAction(id, error))
            {
                uvsr::log::warning(
                    "UI action failed: %s",
                    error.c_str());
                return false;
            }
            return true;
        };
        ApplyUiStyle(m_UiDisplayScale);
        int width, height;
        GetDeviceManager()->GetWindowDimensions(width, height);
        ImFont* activeUiFont = m_UiBodyFont->GetScaledFont();
        m_SettingsPanelMarginPixels = static_cast<uint32_t>(
            std::max(
                1.f,
                std::round(g_UiSpacingTokens.section)));
        const ImGuiViewport* mainViewport =
            ImGui::GetMainViewport();
        const UiLayoutRect workRectangle = {
            mainViewport->WorkPos.x,
            mainViewport->WorkPos.y,
            mainViewport->WorkPos.x + mainViewport->WorkSize.x,
            mainViewport->WorkPos.y + mainViewport->WorkSize.y
        };
        ImGui::PushFont(activeUiFont);
        const float performanceCollapsedHeight = GetSettingsCollapsedWindowHeight(
            ImGui::GetStyle(), ImGui::GetFontSize());
        const float minimumSettingsHeight =
            std::max(
                performanceCollapsedHeight + ImGui::GetFontSize() + g_UiSpacingTokens.tight,
                ImGui::GetStyle().WindowMinSize.y);
        const float panelSeparation =
            g_UiSpacingTokens.tight;
        ImGui::PopFont();
        const float panelStackMaximumBottom =
            workRectangle.maxY - float(m_SettingsPanelMarginPixels);
        const bool sceneLoading = m_app->IsSceneBusy();
        if (sceneLoading)
        {
            if (!m_WasSceneLoading)
            {
                m_WasSceneLoading = true;
                m_DisplayedFrameTime = 0.0;
                m_StatSnapshotElapsed = 0.0;
                m_StatFrameTimeSum = 0.0;
                m_StatFrameTimeCount = 0;
                for (std::string& value : m_PerformanceStatValues)
                    value.clear();
                m_HasAppliedStatSnapshot = false;
                m_SceneLoadCounterStart =
                    std::chrono::steady_clock::now();
                m_SceneLoadHistoryKey = m_app->GetCurrentSceneName();
                m_SceneLoadFailed = false;
            }

            BeginFullScreenWindow();
            ImGui::PushFont(activeUiFont);

            const auto& stats = Scene::GetLoadingStats();
            const uint32_t objectsLoaded = stats.ObjectsLoaded.load();
            const uint32_t objectsTotal = std::max(
                stats.ObjectsTotal.load(),
                objectsLoaded);
            const uint64_t importStepsCompleted =
                stats.ImportStepsCompleted.load();
            const uint64_t importStepsTotal = std::max(
                stats.ImportStepsTotal.load(),
                importStepsCompleted);
            const uint32_t texturesDecoded =
                m_app->GetTextureCache()->GetNumberOfLoadedTextures();
            const uint32_t texturesReady =
                m_app->GetTextureCache()->GetNumberOfFinalizedTextures();
            const uint32_t texturesTotal = std::max(
                m_app->GetTextureCache()->GetNumberOfRequestedTextures(),
                std::max(texturesDecoded, texturesReady));
            char messageBuffer[512];
            const std::string sceneDisplayName =
                m_app->GetCurrentSceneDisplayName();
            const char* loadingPhase =
                m_app->IsSceneGpuUploadPending()
                    ? "Uploading mesh buffers in bounded chunks"
                    : "Importing and preparing scene data";
            const uint64_t elapsedLoadMilliseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() -
                    m_SceneLoadCounterStart).count());
            const uint64_t elapsedLoadTicks =
                ResolveSceneLoadElapsedTicks(elapsedLoadMilliseconds);
            uint64_t averageLoadTicks = 0u;
            const auto sceneTiming = m_SceneLoadTiming.byScene.find(
                m_SceneLoadHistoryKey);
            if (sceneTiming != m_SceneLoadTiming.byScene.end())
            {
                averageLoadTicks = ResolveAverageSceneLoadTicks(
                    sceneTiming->second);
            }
            if (averageLoadTicks == 0u)
            {
                averageLoadTicks = ResolveAverageSceneLoadTicks(
                    m_SceneLoadTiming.allScenes);
            }
            const std::string averageLoadLabel = averageLoadTicks > 0u
                ? std::to_string(averageLoadTicks)
                : "--";
            snprintf(
                messageBuffer,
                std::size(messageBuffer),
                "Loading scene: %s, please wait%.*s\n"
                "%s: %llu/%s\n"
                "Objects: %u/%u / Import steps: %llu/%llu / "
                "Textures decoded: %u/%u / GPU ready: %u/%u",
                sceneDisplayName.c_str(),
                1 + int(elapsedLoadMilliseconds / 500u % 3u), "...",
                loadingPhase,
                static_cast<unsigned long long>(elapsedLoadTicks),
                averageLoadLabel.c_str(),
                objectsLoaded,
                objectsTotal,
                static_cast<unsigned long long>(importStepsCompleted),
                static_cast<unsigned long long>(importStepsTotal),
                texturesDecoded,
                texturesTotal,
                texturesReady,
                texturesTotal);
            DrawScreenCenteredText(messageBuffer);
            ImGui::PopFont();
            EndFullScreenWindow();

            return;
        }
        if (m_WasSceneLoading)
        {
            m_SceneLoadFailed = m_app->HasSceneLoadFailure() ||
                !m_app->IsSceneLoaded();
            if (!m_SceneLoadFailed)
            {
                const uint64_t completedLoadMilliseconds =
                    static_cast<uint64_t>(
                        std::chrono::duration_cast<
                            std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() -
                            m_SceneLoadCounterStart).count());
                RecordSceneLoadDuration(
                    m_SceneLoadTiming.allScenes,
                    completedLoadMilliseconds);
                if (!RecordBoundedSceneLoadDuration(
                        m_SceneLoadTiming.byScene,
                        m_SceneLoadHistoryKey,
                        completedLoadMilliseconds))
                {
                    uvsr::log::warning(
                        "Scene loading history rejected the key '%s'",
                        m_SceneLoadHistoryKey.c_str());
                }
                SaveSceneLoadTimingDatabase();
            }
        }
        m_WasSceneLoading = false;

        ImGui::PushFont(activeUiFont);

        float const fontSize = ImGui::GetFontSize();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float settingsControlWidth = 208.f * m_UiDisplayScale;
        RefreshSettingsSnapshot();

        UpdateStatSnapshot(width, height);
        if (!m_ui.ShowUI)
        {
            ImGui::PopFont();
            return;
        }
        const std::string performanceLine = m_PerformanceStatValues[0] + " / " +
            m_PerformanceStatValues[3] + " / " + m_PerformanceStatValues[1] + " / " + m_PerformanceStatValues[2];

        // fixed controls and a separate picker lane must fit the viewport.
        const float settingsPanelMarginPixels =
            float(m_SettingsPanelMarginPixels);
        const float availableWindowWidth =
            std::max(
                1.f,
                workRectangle.maxX -
                    workRectangle.minX -
                    settingsPanelMarginPixels * 2.f);
        const float colorPickerMinimumContentWidth =
            ImGui::GetFrameHeight() * 3.f + style.ItemInnerSpacing.x * 2.f;
        const float colorPickerPopupHorizontalPadding =
            style.WindowPadding.x + style.ItemInnerSpacing.x;
        const float colorPickerMinimumLaneWidth =
            colorPickerMinimumContentWidth +
            colorPickerPopupHorizontalPadding * 2.f;
        const float settingsWindowMaximumWidth =
            std::max(
                1.f,
                availableWindowWidth -
                    colorPickerMinimumLaneWidth);
        const float settingsWindowMinimumWidth =
            settingsControlWidth +
            style.WindowPadding.x * 4.f +
            style.ScrollbarSize;
        const float settingsDesiredWidth = settingsControlWidth +
            ImGui::CalcTextSize("Override Visual Maxes").x + ImGui::GetFrameHeight() +
            style.WindowPadding.x * 4.f + style.ScrollbarSize;
        const float settingsWindowWidth = std::min(
            std::max(
                settingsDesiredWidth,
                settingsWindowMinimumWidth),
            settingsWindowMaximumWidth);
        const float performanceWindowTop =
            workRectangle.minY + settingsPanelMarginPixels;
        const float performanceMaximumWindowHeight = std::max(
            performanceCollapsedHeight,
            ResolvePerformanceMaximumWindowHeight(
                panelStackMaximumBottom,
                performanceWindowTop,
                minimumSettingsHeight,
                panelSeparation));
        const RootPanel performance = BeginRootPanel(true,
            ImVec2(workRectangle.minX + settingsPanelMarginPixels, performanceWindowTop),
            settingsWindowWidth, performanceMaximumWindowHeight);
        const RootPanelGeometry performanceGeometry = GetRootPanelGeometry(performance.window);
        const ImRect performanceExpandedContentRect(
            ImVec2(performanceGeometry.content.Min.x, performanceGeometry.summary.Max.y),
            performanceGeometry.content.Max);
        if (performance.expanded)
        {
            DrawPerformancePanelContents(
                settingsControlWidth,
                performanceLine);
        }
        else if (performance.collapsed)
        {
            DrawCompactRootPanelBody(
                performance.window->DrawList,
                performanceGeometry.body,
                performanceGeometry.summary,
                style.WindowRounding,
                performanceLine.c_str());
        }
        const float performanceWindowBottom = performance.window->Pos.y + performance.window->Size.y;
        EndRootPanel();

        const float settingsWindowTop = performanceWindowBottom + panelSeparation;
        const float settingsMaximumWindowHeight =
            std::max(
                1.f,
                panelStackMaximumBottom - settingsWindowTop);
        const RootPanel settings = BeginRootPanel(false,
            ImVec2(workRectangle.minX + settingsPanelMarginPixels, settingsWindowTop),
            settingsWindowWidth, settingsMaximumWindowHeight);
        const RootPanelGeometry settingsGeometry = GetRootPanelGeometry(settings.window);

        ImVec2 expandedSettingsSnapshotMinimum{};
        bool expandedSettingsSnapshotSubmitted = false;
        if (settings.expanded)
        {
            const float snapshotCursorX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(
                snapshotCursorX + g_UiSpacingTokens.tight);
            ImVec4 hiddenSnapshotText =
                ImGui::GetStyleColorVec4(ImGuiCol_Text);
            hiddenSnapshotText.w = 0.f;
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                hiddenSnapshotText);
            ImGui::TextUnformatted(m_SettingsSnapshots.Code().c_str());
            ImGui::PopStyleColor();
            expandedSettingsSnapshotMinimum = ImGui::GetItemRectMin();
            expandedSettingsSnapshotSubmitted = true;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                CopySettingsSnapshot();
            ImGui::SetItemTooltip(
                "Click to copy this versioned settings snapshot code. The "
                "decoder resolves every represented setting from the local "
                "UVSR snapshot catalog.");
            ImGui::SetCursorPosX(snapshotCursorX);

        // The root owns the fixed snapshot line and title-to-content inset.
        // Keeping them outside the scrolling child leaves the code visible
        // above every drawer.
        const float settingsBodyMaxHeight = std::max(
            1.f,
            panelStackMaximumBottom -
                ImGui::GetCursorScreenPos().y - style.WindowPadding.y);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0.f, 0.f), ImVec2(FLT_MAX, settingsBodyMaxHeight));
        ImGui::BeginChild(
            "##SettingsBody",
            ImVec2(0.f, 0.f),
            ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImGuiWindow* settingsBodyWindow =
            ImGui::GetCurrentWindow();
        const int settingsFrame = ImGui::GetFrameCount();
        if (m_SettingsScrollFrame == settingsFrame - 1 &&
            std::abs(settingsBodyWindow->Scroll.y - m_SettingsScrollY) > 0.01f)
        {
            ImGui::CloseUvsrColorPickerPopup();
        }
        m_SettingsScrollFrame = settingsFrame;
        m_SettingsScrollY = settingsBodyWindow->Scroll.y;
        const float colorPickerMaximumBottom = std::min(
            panelStackMaximumBottom,
            settingsBodyWindow->ParentWindow->Pos.y +
                settingsBodyWindow->ParentWindow->Size.y);
        ImGui::SetUvsrColorPickerBounds(settingsBodyWindow->InnerRect.Max.x, colorPickerMaximumBottom);
        DrawGeneralDrawer(settingsControlWidth);

        if (BeginToggleRegion(
                "##PathingDrawerVisibility",
                m_ui.Lighting == LightingSolution::PathTracing))
        {
            DrawPathingDrawer(settingsControlWidth);
            EndControlRegion();
        }

        const auto drawNoiseSettingsControls = [&] (
            const char* identifier,
            bool nestedResetIcons,
            const std::array<SettingId, 3>& settingIds)
        {
            const std::string patternLabel =
                std::string("Noise Pattern##") + identifier;
            SetNextLabeledControlWidth("Noise Pattern", settingsControlWidth);
            DrawUiTokenCombo(
                patternLabel.c_str(),
                settingIds[0],
                "Choose a precomputed R8 spatial or spatiotemporal noise "
                "texture.",
                nestedResetIcons);

            const std::string resolutionLabel =
                std::string("Noise Resolution##") + identifier;
            SetNextLabeledControlWidth(
                "Noise Resolution", settingsControlWidth);
            DrawUiTokenCombo(
                resolutionLabel.c_str(),
                settingIds[1],
                "Choose the centered tile resolution used by this noise "
                "texture.",
                nestedResetIcons);

            const std::string animateLabel =
                std::string("Animate Samples##") + identifier;
            const std::string animateReset = std::string(identifier) + "NoiseAnimate";
            DrawUiBoolean(animateLabel.c_str(), settingIds[2],
                "Advance the noise sequence after each successful effect dispatch.",
                false, nullptr, animateReset.c_str());
        };

        const auto drawDirectionalRayShadowControls = [&]()
        {
            if (!BeginSettingsTree(
                    "Ray Traced Shadows##Shadows",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Trace directional-light visibility using the shared shadow sample count and emitter size."))
            {
                return;
            }

            const DirectionalShadowSettings& settings =
                m_ui.DirectionalShadows;
            const bool available =
                IsSettingAvailable(SettingId::ShadowsRayTracedEnabled);
            if (available || settings.enabled)
            {
            bool shadowsEnabled = settings.enabled;
            if (ImGui::Checkbox(
                    "Enabled##DirectionalRayShadows",
                    &shadowsEnabled))
            {
                ApplyUiSetting(
                    SettingId::ShadowsRayTracedEnabled,
                    UiSettingsValue::Boolean(shadowsEnabled));
            }
            ImGui::SetItemTooltip("Trace direct directional-light visibility at each visible surface.");
            DrawUiReset(SettingId::ShadowsRayTracedEnabled, false, "DirectionalRayShadowsEnabled");
            }

            if (BeginToggleRegion(
                    "##DirectionalRayShadowControls", settings.enabled))
            {
                DrawUiBoolean("Hard Shadows", SettingId::ShadowsRayTracedHard,
                    "Use one shadow ray and override all light emitter angles and radii to zero. Stored sample counts and emitter sizes return when disabled.");
                int shadowSamples = int(ResolveRayShadowSampleCount(settings));
                ImGui::BeginDisabled(settings.hardShadows);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (DrawBoundedSlider("Samples Per Pixel##DirectionalRayShadows", &shadowSamples,
                        1, 64, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
                {
                    const int sampleCount = 1 << std::clamp(int(std::lround(std::log2(double(std::max(1, shadowSamples))))), 0, 6);
                    ApplyUiSetting(SettingId::ShadowsRayTracedSamplesPerPixel,
                        UiSettingsValue::Token(std::to_string(sampleCount)));
                }
                ImGui::SetItemTooltip("Shadow rays per pixel for the directional light and flashlight. Zero-size emitters need only one ray. Path tracing uses its own path sample count.");
                DrawUiReset(SettingId::ShadowsRayTracedSamplesPerPixel);
                ImGui::EndDisabled();
                SetNextLabeledControlWidth("Maximum Distance", settingsControlWidth);
                DrawUiRoundedTokenCombo(
                    "Maximum Distance", SettingId::ShadowsRayTracedMaxDistance, false, false);
                const UiSettingRange rayBiasRange =
                    GetUiSettingRange(
                        SettingId::ShadowsRayTracedRayBias);
                float rayBias = settings.rayBias;
                if (DrawBoundedSlider(
                        "Ray Bias",
                        &rayBias,
                        rayBiasRange.safeMinimum,
                        rayBiasRange.safeMaximum,
                        rayBiasRange.trackMinimum,
                        rayBiasRange.trackMaximum,
                        "%.4f"))
                {
                    ApplyUiSetting(
                        SettingId::ShadowsRayTracedRayBias,
                        UiSettingsValue::Float(rayBias));
                }
                ImGui::TextDisabled(
                    "Direct visibility: %ux receiver samples.",
                    1u);
                EndControlRegion();
            }
            if (!available)
                ImGui::TextDisabled("DXR 1.1 and a primary directional light are required.");
            EndSettingsTree();
        };

        if (BeginToggleRegion(
                "##ShadowsDrawerVisibility",
                m_ui.Lighting == LightingSolution::RayMarching))
        {
        const bool shadowsOpen = DrawCollapsingHeader(
            "Shadow", "Configure ray traced direct light shadows.");
        if (shadowsOpen)
        {
            BeginDrawerBody(
                "##ShadowsBody",
                settingsControlWidth);

            const bool directionalVisibilityAvailable =
                m_app->HasPrimaryDirectionalLight();
            const bool rayTracedShadowHardwareAvailable =
                m_app->HasDirectionalRayVisibilityHardwareSupport();
            if (!directionalVisibilityAvailable)
            {
                ImGui::TextDisabled(
                    "Directional techniques require a directional light.");
            }
            else if (!rayTracedShadowHardwareAvailable)
            {
                ImGui::TextDisabled(
                    "Ray traced shadows require DXR 1.1 support.");
            }

            drawDirectionalRayShadowControls();
            EndDrawerBody();
        }
        EndControlRegion();
        }

        const auto& lights = m_app->GetEditableLights();
        const bool lightsOpen = DrawCollapsingHeader(
            "Light", "Show scene light controls.");
        if (lightsOpen)
        {
            BeginDrawerBody(
                "##LightsBody",
                settingsControlWidth);
            if (m_ui.DirectionalShadows.hardShadows)
                ImGui::TextWrapped("Hard Shadows overrides emitter sizes to zero. Stored sizes below return when it is off.");
            if (!lights.empty())
            {
                UiSettingsValue selectedLightValue;
                std::string selectedLightError;
                const bool selectedLightRead = ReadSettingValue(
                    SettingId::LightSelected,
                    selectedLightValue,
                    selectedLightError);
                if (!selectedLightRead)
                {
                    uvsr::log::warning(
                        "Selected light read failed: %s",
                        selectedLightError.c_str());
                }
                ImGui::SetNextItemWidth(settingsControlWidth);
                const bool lightComboOpen = ImGui::BeginCombo(
                    "Select Light", m_SelectedLight ? m_SelectedLight->GetName().c_str() : "(None)");
                ImGui::SetItemTooltip("Choose a light to edit.");
                if (lightComboOpen)
                {
                    for (std::size_t index = 0u;
                        index < lights.size(); ++index)
                    {
                        const auto& light = lights[index];
                        const std::string selector =
                            FormatSettingsSnapshotLightToken(
                                index,
                                light->GetName());
                        const bool selected = selectedLightRead &&
                            selectedLightValue.kind ==
                                UiSettingsValueKind::Selector &&
                            selectedLightValue.text == selector;
                        DrawDropdownOption(
                            light->GetName().c_str(),
                            selected,
                            [this, selector]()
                            {
                                ApplyUiSetting(
                                    SettingId::LightSelected,
                                    UiSettingsValue::Selector(
                                        selector));
                            });
                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                DrawUiReset(SettingId::LightSelected, false, "Selected Light", "Select the scene's primary directional light.");

                if (m_SelectedLight)
                {
                    if (m_app->IsFlashlight(m_SelectedLight))
                    {
                        const FlashlightSettings& flashlight =
                            m_ui.Flashlight;
                        DrawUiBoolean(
                            "Enabled",
                            SettingId::LightSelectedFlashlightEnabled,
                            "Turn the camera flashlight on or off. Plain F "
                            "uses the same setting.");
                        if (BeginToggleRegion(
                                "##RayMarchingFlashlightVisibility",
                                m_ui.Lighting ==
                                    LightingSolution::RayMarching))
                        {
                        DrawUiBoolean(
                            "Cast Shadows",
                            SettingId::LightSelectedFlashlightCastShadows,
                            "Trace flashlight visibility through the shared "
                            "scene representation.");
                        if (BeginToggleRegion(
                                "##FlashlightShadowControls",
                                flashlight.castShadows))
                        {
                            EndControlRegion();
                        }
                        EndControlRegion();
                        }

                        DrawUiBoolean(
                            "Realistic Flashlight",
                            SettingId::LightSelectedFlashlightRealistic,
                            "Add a lens hotspot, bounded sway, and aim "
                            "correction inside one physical spot light.");

                        if (BeginToggleRegion(
                                "##RealisticFlashlightControls",
                                flashlight.realisticLens))
                        {
                            DrawUiFloat(
                                "Hotspot Size",
                                SettingId::LightSelectedFlashlightHotspotSize,
                                "%.2f",
                                "Set the focused lens hotspot width relative "
                                "to the complete beam.");

                            DrawUiFloat(
                                "Hotspot Strength",
                                SettingId::LightSelectedFlashlightHotspotStrength,
                                "%.2f",
                                "Move peak candela from the broad spill into "
                                "the focused lens hotspot.");

                            static constexpr char
                                FlashlightStationaryWhenIdleTooltip[] =
                                    "Freeze the flashlight pose when the active "
                                    "camera rests. Camera motion or motion-setting "
                                    "changes resume it.";
                            static_assert(
                                sizeof(FlashlightStationaryWhenIdleTooltip) -
                                    1u <=
                                120u);
                            DrawUiBoolean(
                                "Stationary When Idle",
                                SettingId::LightSelectedFlashlightStationaryWhenIdle,
                                FlashlightStationaryWhenIdleTooltip);

                            DrawUiFloat(
                                "Sway",
                                SettingId::LightSelectedFlashlightSway,
                                "%.2f degrees",
                                "Set the maximum subtle handheld aim motion. "
                                "Zero keeps the corrected beam perfectly still.");

                            DrawUiFloat(
                                "Aim Correction",
                                SettingId::LightSelectedFlashlightAimCorrection,
                                "%.2f s",
                                "Set the half-life for the beam to catch up "
                                "after the camera turns.");

                            EndControlRegion();
                        }

                        DrawUiFloat(
                            "Brightness",
                            SettingId::LightSelectedFlashlightBrightness,
                            "%.0f candela",
                            "Set the peak on-axis luminous intensity.",
                            ImGuiSliderFlags_Logarithmic);

                        DrawUiFloat(
                            "Beam Size",
                            SettingId::LightSelectedFlashlightBeamSize,
                            "%.1f degrees",
                            "Set the full horizontal and vertical outer beam "
                            "width.");

                        DrawUiFloat(
                            m_ui.DirectionalShadows.hardShadows ? "Stored Angular Size" : "Angular Size",
                            SettingId::LightSelectedFlashlightAngularSize,
                            "%.2f degrees",
                            "Set the apparent diameter of the analytical "
                            "spherical emitter at one metre; apparent size "
                            "varies with surface distance.");

                        DrawUiFloat(
                            "Beam Roundness",
                            SettingId::LightSelectedFlashlightBeamRoundness,
                            "%.2f",
                            "Morph the beam footprint from a softly rounded "
                            "square to an exact circle.");

                        DrawUiFloat(
                            "Edge Softness",
                            SettingId::LightSelectedFlashlightEdgeSoftness,
                            "%.2f",
                            "Set the falloff width without changing the "
                            "projected beam shape.");

                        DrawUiFloat(
                            "Range",
                            SettingId::LightSelectedFlashlightRange,
                            "%.1f m",
                            "Set the finite distance where the beam fades out.",
                            ImGuiSliderFlags_Logarithmic);

                        DrawUiColor(
                            "Color", SettingId::LightSelectedColor,
                            "Set the flashlight's scene-linear red, green, and "
                            "blue color.", "Flashlight Color");

                        DrawUiFloat(
                            "Horizontal Offset",
                            SettingId::LightSelectedFlashlightHorizontalOffset,
                            "%.1f centimeters",
                            "Move the flashlight left or right from the camera "
                            "by up to 40 centimeters.");

                        DrawUiFloat(
                            "Vertical Offset",
                            SettingId::LightSelectedFlashlightVerticalOffset,
                            "%.1f centimeters",
                            "Move the flashlight down or up from the camera "
                            "by up to 40 centimeters.");
                    }
                    else
                    {
                    const auto drawLightDirection =
                        [&](const Light& light, bool directional)
                        {
                            auto [azimuth, elevation] =
                                GetCommandLightAngles(
                                    light.GetDirection(),
                                    directional);
                            const auto drawAngle = [&] (
                                const char* label,
                                SettingId id,
                                float value)
                            {
                                const UiSettingRange range =
                                    GetUiSettingRange(id);
                                if (DrawBoundedSlider(
                                        label,
                                        &value,
                                        range.safeMinimum,
                                        range.safeMaximum,
                                        range.trackMinimum,
                                        range.trackMaximum,
                                        "%.1f degrees"))
                                {
                                    ApplyUiSetting(
                                        id,
                                        UiSettingsValue::Float(value));
                                }
                                ImGui::SetItemTooltip(
                                    "Set the selected light's direction.");
                            };
                            drawAngle(
                                "Azimuth",
                                SettingId::LightSelectedAzimuth,
                                azimuth);
                            drawAngle(
                                "Elevation",
                                SettingId::LightSelectedElevation,
                                elevation);
                            if (DrawPresetResetIcon(
                                    "Light Direction",
                                    IsUiSettingChanged(
                                        SettingId::LightSelectedAzimuth) ||
                                    IsUiSettingChanged(
                                        SettingId::LightSelectedElevation)))
                            {
                                ResetUiSetting(
                                    SettingId::LightSelectedAzimuth);
                                ResetUiSetting(
                                    SettingId::LightSelectedElevation);
                            }
                        };

                    switch (m_SelectedLight->GetLightType())
                    {
                    case UVSR_LIGHT_TYPE_DIRECTIONAL:
                    {
                        const auto& light = static_cast<const DirectionalLight&>(
                            *m_SelectedLight);
                        drawLightDirection(light, true);
                        DrawUiColor(
                            "Color",
                            SettingId::LightSelectedColor,
                            "Set the selected light's color.",
                            "Light Color");
                        DrawUiFloat(
                            "Irradiance",
                            SettingId::LightSelectedIrradiance,
                            "%.2f",
                            "Set the directional light irradiance.",
                            ImGuiSliderFlags_Logarithmic);
                        DrawUiFloat(
                            m_ui.DirectionalShadows.hardShadows ? "Stored Angular Size" : "Angular Size",
                            SettingId::LightSelectedAngularSize,
                            "%.2f degrees",
                            "Set the directional light's angular diameter. Zero "
                            "degrees creates a zero extent emitter with hard shadows.");
                        break;
                    }
                    case UVSR_LIGHT_TYPE_POINT:
                    {
                        DrawUiFloat(
                            m_ui.DirectionalShadows.hardShadows ? "Stored Radius" : "Radius",
                            SettingId::LightSelectedRadius,
                            "%.3f",
                            "Set the point light radius.",
                            ImGuiSliderFlags_Logarithmic);
                        DrawUiColor(
                            "Color",
                            SettingId::LightSelectedColor,
                            "Set the selected light's color.",
                            "Light Color");
                        DrawUiFloat(
                            "Intensity",
                            SettingId::LightSelectedIntensity,
                            "%.2f",
                            "Set the point light intensity.",
                            ImGuiSliderFlags_Logarithmic);
                        break;
                    }
                    case UVSR_LIGHT_TYPE_SPOT:
                    {
                        const auto& light = static_cast<const SpotLight&>(
                            *m_SelectedLight);
                        drawLightDirection(light, false);
                        DrawUiFloat(
                            m_ui.DirectionalShadows.hardShadows ? "Stored Radius" : "Radius",
                            SettingId::LightSelectedRadius,
                            "%.3f",
                            "Set the spot light radius.",
                            ImGuiSliderFlags_Logarithmic);
                        DrawUiColor(
                            "Color",
                            SettingId::LightSelectedColor,
                            "Set the selected light's color.",
                            "Light Color");
                        DrawUiFloat(
                            "Intensity",
                            SettingId::LightSelectedIntensity,
                            "%.2f",
                            "Set the spot light intensity.",
                            ImGuiSliderFlags_Logarithmic);
                        DrawUiFloat(
                            "Inner Angle",
                            SettingId::LightSelectedInnerAngle,
                            "%.1f degrees",
                            "Set the full bright spot cone angle.");
                        DrawUiFloat(
                            "Outer Angle",
                            SettingId::LightSelectedOuterAngle,
                            "%.1f degrees",
                            "Set the outer spot cone angle.");
                        break;
                    }
                    default:
                        ImGui::TextDisabled(
                            "This light type has no editable settings.");
                        break;
                    }
                    }
                }
            }

            EndDrawerBody();
        }
        ImGui::Spacing();

        const bool skyOpen = DrawCollapsingHeader(
            "Sky", "Show sky controls.");
        if (skyOpen)
        {
            BeginDrawerBody(
                "##SkyBody",
                settingsControlWidth);

            SetNextLabeledControlWidth(
                "Environment##SkyEnvironment",
                settingsControlWidth);
            DrawUiRoundedTokenCombo(
                "Environment##SkyEnvironment",
                SettingId::SkyEnvironment);
            ImGui::SetItemTooltip(
                "Choose the imported radiance source used by image-based "
                "lighting and the "
                "optional matching background.");
            DrawUiReset(SettingId::SkyEnvironment, false, "Environment Source");

            DrawUiFloat(
                "Environment Exposure##ImageBasedLighting",
                SettingId::SkyExposure,
                "%+.2f",
                "Scale only environment lighting and its background. Direct lights and emissive materials keep their intensity. Image Exposure in Tonemapper adjusts the whole image.");
            DrawUiBoolean(
                "Show Environment Background",
                SettingId::SkyEnvironmentBackground,
                "Show the same environment used for lighting.");

            if (BeginSettingsTree(
                    "Auto Environment Exposure##Sky",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Adapt display exposure without changing the established "
                    "tonemapper or physical lighting."))
            {
                DrawUiBoolean(
                    "Enable##AutoExposure",
                    SettingId::SkyAutoExposureEnabled,
                    "Adapt display exposure to the median scene luminance. "
                    "Lighting, ray effects, and their histories are unchanged.");
                if (BeginToggleRegion(
                        "##AutoExposureControls",
                        m_ui.AutoExposure.enabled))
                {
                    DrawUiFloat(
                        "Compensation",
                        SettingId::SkyAutoExposureExposureCompensation,
                        "%+.2f EV",
                        "Bias the bounded automatic exposure result in "
                        "exposure-value stops.");
                    DrawUiFloat(
                        "Maximum Brightening",
                        SettingId::SkyAutoExposureMaximumBrightening,
                        "%.2f EV",
                        "Limit how far automatic metering may raise exposure. "
                        "Compensation is applied afterward.");
                    DrawUiFloat(
                        "Maximum Darkening",
                        SettingId::SkyAutoExposureMaximumDarkening,
                        "%.2f EV",
                        "Limit how far automatic metering may lower exposure. "
                        "Compensation is applied afterward.");
                    DrawUiFloat(
                        "Adjustment Period",
                        SettingId::SkyAutoExposureAdjustmentPeriod,
                        "%.2f s",
                        "Set the half-life of exposure adaptation. This affects "
                        "only display adaptation, not lighting or effect histories.");
                    EndControlRegion();
                }
                EndSettingsTree();
            }

            if (BeginToggleRegion(
                    "##RayMarchingAmbientFill",
                    m_ui.Lighting == LightingSolution::RayMarching))
            {
            DrawUiBoolean(
                "Ambient Fill",
                SettingId::SkyAmbientFillEnabled,
                "Enable diffuse/specular environment fill. Disable it to "
                "isolate direct lights; settings persist, and Occlusion "
                "needs it.");
            if (BeginToggleRegion(
                    "##AmbientFillControls",
                    m_ui.EnableAmbientFill))
            {
                DrawUiBoolean(
                    "Diffuse Environment",
                    SettingId::SkyDiffuseIbl,
                    "Use the selected environment for diffuse lighting.");
                if (BeginToggleRegion(
                        "##DiffuseIblControls",
                        m_ui.EnableDiffuseIbl))
                {
                    DrawUiFloat(
                        "Diffuse Strength##ImageBasedLighting",
                        SettingId::SkyDiffuseIblStrength,
                        "%.2f",
                        "Scale diffuse environment lighting after exposure.");
                    EndControlRegion();
                }

                DrawUiBoolean(
                    "Specular Environment",
                    SettingId::SkySpecularIbl,
                    "Use the selected environment for specular reflections.");
                if (BeginToggleRegion(
                        "##SpecularIblControls",
                        m_ui.EnableSpecularIbl))
                {
                    DrawUiFloat(
                        "Specular Strength##ImageBasedLighting",
                        SettingId::SkySpecularIblStrength,
                        "%.2f",
                        "Scale specular environment lighting after exposure.");
                    EndControlRegion();
                }

                EndControlRegion();
            }
            EndControlRegion();
            }

            if (BeginToggleRegion(
                    "##RayMarchingSkyVisibility",
                    m_ui.Lighting == LightingSolution::RayMarching))
            {
            ImGui::Spacing();
            if (BeginSettingsTree(
                    "Ray Traced Sky Visibility##Sky",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure ray traced environment visibility. This effect "
                    "section remains independently collapsible while enabled."))
            {
            const RayTracedSkyVisibilitySettings& skyVisibility =
                m_ui.RayTracedSkyVisibility;
            const bool skyVisibilityAvailable =
                IsSettingAvailable(SettingId::SkyVisibilityEnabled);
            const bool disableSkyVisibilityEnable =
                !skyVisibilityAvailable && !skyVisibility.enabled;
            if (!disableSkyVisibilityEnable)
            {
            DrawUiBoolean(
                "Enable##RayTracedSkyVisibility",
                SettingId::SkyVisibilityEnabled,
                "Trace current frame world space visibility for the selected "
                "diffuse and specular environment lighting consumers.");
            }

            if (BeginToggleRegion(
                    "##RayTracedSkyVisibilityControls",
                    skyVisibility.enabled && skyVisibilityAvailable))
            {
                DrawUiBoolean(
                    "Effect Diffuse##RayTracedSkyVisibility",
                    SettingId::SkyVisibilityDiffuseIbl,
                    "Apply the scalar visibility to diffuse environment "
                    "lighting before final composition and GI source "
                    "radiance.");
                DrawUiBoolean(
                    "Effect Specular##RayTracedSkyVisibility",
                    SettingId::SkyVisibilitySpecularIbl,
                    "Apply cosine-weighted normal-hemisphere visibility to "
                    "specular lighting; it ignores reflection direction and "
                    "roughness.");

                const UiSettingsTypedDomain& sampleDomain =
                    GetUiSettingDefinition(
                        SettingId::SkyVisibilitySamplesPerPixel).typedDomain;
                std::int64_t minimumSamples = 1;
                std::int64_t maximumSamples = 1;
                const bool parsedSampleBounds =
                    TryParseCommandInteger(
                        sampleDomain.tokens.front(), minimumSamples) &&
                    TryParseCommandInteger(
                        sampleDomain.tokens[
                            sampleDomain.tokenCount - 1u],
                        maximumSamples);
                assert(parsedSampleBounds);
                int sampleRate = 1 << skyVisibility.sampleRateLog2;
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (DrawBoundedSlider("Samples Per Pixel##RayTracedSkyVisibility", &sampleRate, static_cast<int>(minimumSamples), static_cast<int>(maximumSamples),
                        static_cast<int>(minimumSamples), static_cast<int>(maximumSamples),
                        "%d",
                        ImGuiSliderFlags_AlwaysClamp |
                            ImGuiSliderFlags_Logarithmic))
                {
                    std::size_t closestIndex = 0u;
                    double closestDistance =
                        std::numeric_limits<double>::infinity();
                    for (std::size_t index = 0u;
                        index < sampleDomain.tokenCount; ++index)
                    {
                        std::int64_t candidateSamples = 1;
                        if (!TryParseCommandInteger(
                                sampleDomain.tokens[index],
                                candidateSamples))
                        {
                            continue;
                        }
                        const double distance = std::abs(
                            std::log2(double(std::max(1, sampleRate))) -
                            std::log2(double(candidateSamples)));
                        if (distance < closestDistance)
                        {
                            closestDistance = distance;
                            closestIndex = index;
                        }
                    }
                    ApplyUiSetting(
                        SettingId::SkyVisibilitySamplesPerPixel,
                        UiSettingsValue::Token(
                            std::string(sampleDomain.tokens[closestIndex])));
                }
                ImGui::SetItemTooltip(
                    "Trace and average 1 to 64 cosine-weighted normal-hemisphere "
                    "rays per pixel.");
                DrawUiReset(SettingId::SkyVisibilitySamplesPerPixel, false, "RayTracedSkyVisibilitySamples");

                DrawUiBoolean(
                    "Specify Noise##RayTracedSkyVisibility",
                    SettingId::SkyVisibilitySpecifyNoise,
                    "Use custom noise sampling for this effect only. This "
                    "does not change the noise sampling used by any other "
                    "effect.");
                if (BeginToggleRegion(
                        "##RayTracedSkyVisibilityCustomNoise",
                        skyVisibility.noise.specifyNoise))
                {
                    drawNoiseSettingsControls(
                        "RayTracedSkyVisibility",
                        true,
                        {
                            SettingId::SkyVisibilityNoisePattern,
                            SettingId::SkyVisibilityNoiseResolution,
                            SettingId::SkyVisibilityAnimateSamples
                        });
                    EndControlRegion();
                }

                SetNextLabeledControlWidth(
                    "Max Distance##RayTracedSkyVisibility",
                    settingsControlWidth);
                DrawUiTokenCombo(
                    "Max Distance##RayTracedSkyVisibility",
                    SettingId::SkyVisibilityMaxDistance,
                    "Max uses the scene diagonal. Finite distances ignore "
                    "farther blockers and provide bounded, not exact, sky "
                    "visibility.");

                DrawUiFloat(
                    "Ray Bias##RayTracedSkyVisibility",
                    SettingId::SkyVisibilityRayBias,
                    "%.4f",
                    "Offset the ray origin along the view-facing raster-"
                    "triangle normal; large values can detach contact occlusion.");

                const WorldSpaceRepresentationStatus& status =
                    m_app->GetWorldSpaceRepresentationStatus();
                if (status.state ==
                        WorldSpaceRepresentationState::BuildingBlas ||
                    status.state ==
                        WorldSpaceRepresentationState::BuildingTlas)
                {
                    ImGui::TextDisabled(
                        "Preparing world hierarchy: BLAS %u/%u.",
                        status.builtBlasCount,
                        status.totalBlasCount);
                }
                EndControlRegion();
            }
            EndSettingsTree();
            }
            EndControlRegion();
            }

            EndDrawerBody();
        }
        ImGui::Spacing();

        DrawPostprocessDrawer(settingsControlWidth);
        DrawMaterialDrawer(settingsControlWidth);
        const bool noiseOpen = DrawCollapsingHeader(
            "Noise",
            "Configure the shared precomputed noise used by rendering effects.");
        if (noiseOpen)
        {
            BeginDrawerBody("##NoiseBody", settingsControlWidth);
            const auto drawSampleAccumulationControls = [&]()
            {
                if (m_ui.Lighting == LightingSolution::PathTracing)
                {
                    ImGui::TextDisabled(
                        "Path Tracing always advances one cumulative mean while inputs remain stable.");
                    return;
                }
                DrawUiBoolean(
                    "Enable##SampleAccumulation",
                    SettingId::NoiseAccumulateSamples,
                    "Average each successful Ray Tracing sample until camera, scene, resolution, or lighting changes.");
            };

            drawNoiseSettingsControls(
                "GlobalNoise",
                false,
                {
                    SettingId::NoisePattern,
                    SettingId::NoiseResolution,
                    SettingId::NoiseAnimateSamples
                });
            ImGui::TextDisabled(
                "Resident texture memory: %.2f MiB",
                double(m_app->GetNoiseTextureResidentBytes()) /
                    (1024.0 * 1024.0));
            if (BeginSettingsTree(
                    "Accumulate Samples##Noise",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Average finite scene-linear samples while the camera, "
                    "scene, and lighting remain still."))
            {
                drawSampleAccumulationControls();
                EndSettingsTree();
            }
            EndDrawerBody();
        }
        ImGui::Spacing();

        const bool debugOpen = DrawCollapsingHeader(
            "Debug",
            "Combine world appearance and effect-specific information views.");
        if (debugOpen)
        {
            BeginDrawerBody("##DebugBody", settingsControlWidth);

            if (BeginSettingsTree(
                    "World##Debug",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Change material presentation without changing lighting effects."))
            {
            SetNextLabeledControlWidth(
                "Materials", settingsControlWidth);
            DrawUiRoundedTokenCombo(
                "Materials",
                SettingId::DebugWorldMaterials);
            ImGui::SetItemTooltip(
                "Choose default materials or white world. This combines with "
                "every effect-specific debug view.");
            DrawUiReset(SettingId::DebugWorldMaterials, true, "DebugWorld");
            EndSettingsTree();
            }

            if (BeginToggleRegion(
                    "##RayMarchingDebugBody",
                    m_ui.Lighting == LightingSolution::RayMarching))
            {
            if (BeginSettingsTree(
                    "Physically Based Lighting##Debug",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Inspect material and environment-lighting information."))
            {
            const bool skyVisibilityDebugAvailable =
                m_ui.RayTracedSkyVisibility.enabled &&
                m_ui.Representation.allowRayTraversal &&
                m_app->SupportsRayTracedSkyVisibility();
            SetNextLabeledControlWidth(
                "Information Filter", settingsControlWidth);
            const SettingId debugPbrId = SettingId::DebugPbrFilter;
            const std::size_t lightingView =
                ReadUiTokenIndex(debugPbrId);
            const std::string lightingViewLabel =
                FormatUiSettingsTokenLabel(debugPbrId, lightingView);
            if (ImGui::BeginCombo(
                    "Information Filter",
                    lightingViewLabel.c_str()))
            {
                const UiSettingsTypedDomain& domain =
                    GetUiSettingDefinition(debugPbrId).typedDomain;
                for (std::size_t index = 0u;
                    index < domain.tokenCount;
                    ++index)
                {
                    const bool selected = index == lightingView;
                    const bool available =
                        domain.tokens[index] != "sky-visibility" ||
                        skyVisibilityDebugAvailable;
                    if (!available)
                        continue;
                    DrawUiTokenOption(debugPbrId, index, selected);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose which material or environment-lighting quantity is "
                "shown. Visibility remains enabled and can still be inspected.");
            DrawUiReset(SettingId::DebugPbrFilter, true, "DebugLighting");
            EndSettingsTree();
            }

            EndControlRegion();
            }

            EndDrawerBody();
        }
        ImGui::Spacing();

        DrawDeveloperDrawer(settingsControlWidth);

        constexpr float ActionButtonCount = 4.f;
        const float actionButtonGap = g_UiSpacingTokens.tight;
        const float actionButtonWidth = std::max(
            1.f,
            (ImGui::GetContentRegionAvail().x -
                actionButtonGap * (ActionButtonCount - 1.f)) /
                ActionButtonCount);

        if (DrawCenteredActionButton("Reset", actionButtonWidth))
            runUiAction(ActionId::ResetSettings);
        ImGui::SetItemTooltip(
            "Restore renderer and interface factory settings without changing "
            "the camera, scene, or graphics adapter.");

        ImGui::SameLine(0.f, actionButtonGap);
        if (DrawCenteredActionButton("Capture", actionButtonWidth))
            runUiAction(ActionId::Capture);
        ImGui::SetItemTooltip("Copy the current frame to the clipboard.");

        ImGui::SameLine(0.f, actionButtonGap);
        if (DrawCenteredActionButton(
                "Zoom",
                actionButtonWidth))
        {
            const SettingId zoomId = SettingId::UiZoom;
            const UiSettingsTypedDomain& zoomDomain =
                GetUiSettingDefinition(zoomId).typedDomain;
            const std::size_t tokenIndex =
                (ReadUiTokenIndex(zoomId) + 1u) %
                zoomDomain.tokenCount;
            ApplyUiSetting(
                zoomId,
                UiSettingsValue::Token(
                    GetUiSettingToken(zoomId, tokenIndex)));
        }
        ImGui::SetItemTooltip(
            "Cycle exact Off, 2x, 3x, 4x, and 5x pixel zoom. Z uses the "
            "same cycle.");

        ImGui::SameLine(0.f, actionButtonGap);
        if (DrawCenteredActionButton("Restart", actionButtonWidth))
            runUiAction(ActionId::Restart);
        ImGui::SetItemTooltip("Restart UVSR.");

        const RootPanelGeometry finalSettingsGeometry = GetRootPanelGeometry(settings.window);
        ImDrawList* settingsDecorationDrawList = settingsBodyWindow->DrawList;
        if (expandedSettingsSnapshotSubmitted)
        {
            settingsDecorationDrawList->PushClipRect(
                finalSettingsGeometry.body.Min,
                finalSettingsGeometry.body.Max,
                false);
            settingsDecorationDrawList->AddText(
                expandedSettingsSnapshotMinimum,
                ImGui::GetColorU32(ImGuiCol_Text),
                m_SettingsSnapshots.Code().c_str());
            settingsDecorationDrawList->PopClipRect();
        }
        ImGui::SetUvsrColorPickerBounds(0.f, 0.f);
        ImGui::EndChild();
        }
        const float rootBodyRounding =
            style.WindowRounding;
        const ImRect settingsBodyRect = settingsGeometry.body;
        if (settings.collapsed &&
            settingsBodyRect.GetHeight() >
                style.WindowPadding.y * 2.f + 2.f)
        {
            const ImRect snapshotHitRect = DrawCompactRootPanelBody(
                settings.window->DrawList,
                settingsBodyRect,
                settingsGeometry.summary,
                rootBodyRounding,
                m_SettingsSnapshots.Code().c_str());
            const bool snapshotHovered = snapshotHitRect.Contains(ImGui::GetIO().MousePos);
            if (snapshotHovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip(
                    "Click to copy this versioned settings snapshot code. "
                    "The decoder resolves every represented setting from "
                    "the local UVSR snapshot catalog.");
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    CopySettingsSnapshot();
            }
        }
        if (settings.collapsed != m_SettingsCollapsed)
        {
            ApplyUiSetting(
                SettingId::UiSettingsCollapsed,
                UiSettingsValue::Boolean(settings.collapsed));
        }
        if (settings.collapsed && m_ui.ShowMaterialDrawer)
            ApplyUiSetting(SettingId::MaterialEditorVisible, UiSettingsValue::Boolean(false));
        EndRootPanel();
        ImGui::PopFont();
    }

void UIRenderer::DrawPostprocessDrawer(float controlWidth)
{
    if (!DrawCollapsingHeader("Postprocess", "Configure image grading and edge filtering."))
    {
        ImGui::Spacing();
        return;
    }
    BeginDrawerBody("##PostprocessBody", controlWidth);
    if (BeginSettingsTree("Tonemapper##Postprocess", ImGuiTreeNodeFlags_DefaultOpen,
            "Optional grading over the neutral AgX display transform."))
    {
        DrawUiBoolean("Enabled##Tonemapper", SettingId::TonemapperEnabled,
            "Enable the tone controls and film LUT. Off keeps neutral AgX HDR compression, which still has a GPU cost. Automatic exposure remains independent.");
        if (BeginToggleRegion("##TonemapperControls", m_ui.ToneMapping.enabled))
        {
            const int preset = FindToneMappingPreset(m_ui.ToneMapping);
            SetNextLabeledControlWidth("Preset##Tonemapper", controlWidth);
            if (ImGui::BeginCombo("Preset##Tonemapper", preset < 0 ? "Custom" : ToneMappingPresets[preset].label))
            {
                for (int index = 0; index < int(ToneMappingPresets.size()); ++index)
                {
                    DrawDropdownOption(ToneMappingPresets[index].label, index == preset, [&]()
                    {
                        constexpr std::array ids = {
                            SettingId::TonemapperExposure, SettingId::TonemapperContrast,
                            SettingId::TonemapperSaturation, SettingId::TonemapperWarmth,
                            SettingId::TonemapperTint, SettingId::TonemapperSlope, SettingId::TonemapperPower };
                        const auto values = ToneMappingGradeValues(ToneMappingPresets[index].settings);
                        for (size_t field = 0; field < ids.size(); ++field)
                            ApplyUiSetting(ids[field], UiSettingsValue::Float(values[field]));
                    });
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Choose a grade. Edits display Custom. Film LUT selection is independent.");
            SetNextLabeledControlWidth("Film LUT", controlWidth);
            DrawUiRoundedTokenCombo("Film LUT", SettingId::TonemapperLut);
            ImGui::SetItemTooltip("Apply a bundled film-style color look. These are artistic simulations, not official film profiles.");
            DrawUiReset(SettingId::TonemapperLut);
            DrawUiFloat("Image Exposure##Tonemapper", SettingId::TonemapperExposure, "%.2f EV",
                "Adjust the whole rendered image, including direct lights and emissive materials, before AgX. This does not change lighting. Environment Exposure in Sky changes only the environment.", ImGuiSliderFlags_None, false, controlWidth);
            DrawUiFloat("Contrast##Tonemapper", SettingId::TonemapperContrast, "%.3f",
                "Increase or reduce contrast.", ImGuiSliderFlags_None, false, controlWidth);
            DrawUiFloat("Saturation##Tonemapper", SettingId::TonemapperSaturation, "%.3f",
                "Increase or reduce color intensity.", ImGuiSliderFlags_None, false, controlWidth);
            DrawUiFloat("Warmth##Tonemapper", SettingId::TonemapperWarmth, "%.3f",
                "Shift colors warmer or cooler.", ImGuiSliderFlags_None, false, controlWidth);
            DrawUiFloat("Tint##Tonemapper", SettingId::TonemapperTint, "%.3f",
                "Shift colors toward green or magenta.", ImGuiSliderFlags_None, false, controlWidth);
            DrawUiFloat("Slope##Tonemapper", SettingId::TonemapperSlope, "%.4f",
                "Scale the color grade. 1.0 is neutral.", ImGuiSliderFlags_None, false, controlWidth);
            DrawUiFloat("Power##Tonemapper", SettingId::TonemapperPower, "%.4f",
                "Below 1 brightens; above 1 darkens.", ImGuiSliderFlags_None, false, controlWidth);
            EndControlRegion();
        }
        EndSettingsTree();
    }
    if (BeginToggleRegion("##PostprocessFxaa", m_ui.Lighting == LightingSolution::RayMarching))
    {
        if (BeginSettingsTree("FXAA##Postprocess", ImGuiTreeNodeFlags_DefaultOpen,
                "Filter edges after the display transform."))
        {
            DrawUiBoolean("Enabled##FXAA", SettingId::AntiAliasingFxaaEnabled,
                "Apply a fast post-tone-map edge filter.");
            if (BeginToggleRegion("##FastApproximateControls", m_ui.AntiAliasing.fastApproximate.enabled))
            {
                const bool qualityCustom =
                    IsUiSettingChanged(SettingId::AntiAliasingFxaaEdgeSharpness) ||
                    IsUiSettingChanged(SettingId::AntiAliasingFxaaEdgeThreshold) ||
                    IsUiSettingChanged(SettingId::AntiAliasingFxaaMinimumEdgeThreshold);
                SetNextLabeledControlWidth("Quality##FastApproximate", controlWidth);
                DrawUiRoundedTokenCombo("Quality##FastApproximate", SettingId::AntiAliasingFxaaQuality, qualityCustom);
                ImGui::SetItemTooltip("Choose FXAA Quality. Tuning changes append (Custom); the arrow restores factory Quality and all FXAA controls.");
                if (DrawPresetResetIcon("FastApproximateQuality",
                        IsUiSettingChanged(SettingId::AntiAliasingFxaaQuality) || qualityCustom))
                    ResetUiSetting(SettingId::AntiAliasingFxaaQuality);

                ImGui::SetNextItemOpen(false, ImGuiCond_Once);
                if (BeginSettingsTree("FXAA Tuning##FastApproximate", ImGuiTreeNodeFlags_None,
                        "Tune edge detection and filtering. This section is closed by default."))
                {
                    SetNextLabeledControlWidth("Edge Sharpness##FastApproximate", controlWidth);
                    DrawUiFloat("Edge Sharpness##FastApproximate", SettingId::AntiAliasingFxaaEdgeSharpness,
                        "%.2f", "Increase to keep the edge filter narrower and sharper.", ImGuiSliderFlags_None, true);
                    SetNextLabeledControlWidth("Relative Edge Threshold##FastApproximate", controlWidth);
                    DrawUiFloat("Relative Edge Threshold##FastApproximate", SettingId::AntiAliasingFxaaEdgeThreshold,
                        "%.3f", "Increase to skip more edges relative to local brightness.", ImGuiSliderFlags_None, true);
                    SetNextLabeledControlWidth("Minimum Edge Threshold##FastApproximate", controlWidth);
                    DrawUiFloat("Minimum Edge Threshold##FastApproximate", SettingId::AntiAliasingFxaaMinimumEdgeThreshold,
                        "%.3f", "Increase to skip more low-contrast edges in dark regions.", ImGuiSliderFlags_None, true);
                    EndSettingsTree();
                }
                EndControlRegion();
            }
            EndSettingsTree();
        }
        EndControlRegion();
    }
    EndDrawerBody();
    ImGui::Spacing();
}
