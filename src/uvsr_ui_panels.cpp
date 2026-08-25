#include "uvsr_internal.h"

auto UIRenderer::TrackAppearanceDrawList(
        std::vector<ImDrawList*>& drawLists,
        ImDrawList* drawList) -> void {
        if (drawList &&
            std::find(
                drawLists.begin(),
                drawLists.end(),
                drawList) == drawLists.end())
        {
            drawLists.push_back(drawList);
        }
    }

auto UIRenderer::TrackSettingsAppearanceDrawList(ImDrawList* drawList) -> void {
        TrackAppearanceDrawList(
            g_SettingsAppearanceDrawLists,
            drawList);
    }

auto UIRenderer::TrackPerformanceAppearanceDrawList(ImDrawList* drawList) -> void {
        TrackAppearanceDrawList(
            g_PerformanceAppearanceDrawLists,
            drawList);
    }

auto UIRenderer::IsSettingsChildLaterInDrawOrder(
        const ImGuiWindow* candidate,
        const ImGuiWindow* current) -> bool {
        if (!current)
            return true;

        const int popupOrder =
            int(candidate->Flags & ImGuiWindowFlags_Popup) -
            int(current->Flags & ImGuiWindowFlags_Popup);
        if (popupOrder != 0)
            return popupOrder > 0;

        const int tooltipOrder =
            int(candidate->Flags & ImGuiWindowFlags_Tooltip) -
            int(current->Flags & ImGuiWindowFlags_Tooltip);
        if (tooltipOrder != 0)
            return tooltipOrder > 0;

        return candidate->BeginOrderWithinParent >
            current->BeginOrderWithinParent;
    }

auto UIRenderer::ResolveFinalSettingsDecorationDrawList(
        ImGuiWindow* window) -> ImDrawList* {
        if (!window)
            return nullptr;

        ImGuiWindow* finalVisibleChild = nullptr;
        for (ImGuiWindow* child : window->DC.ChildWindows)
        {
            if (!child || !child->Active || child->Hidden)
                continue;
            if (IsSettingsChildLaterInDrawOrder(
                    child,
                    finalVisibleChild))
            {
                finalVisibleChild = child;
            }
        }
        return finalVisibleChild
            ? ResolveFinalSettingsDecorationDrawList(finalVisibleChild)
            : window->DrawList;
    }

auto UIRenderer::CaptureCurrentWindowBackdrop(
        UiBackdropRect& backdropRect,
        float rounding) -> void {
        const ImVec2 windowPosition = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        backdropRect.minX = windowPosition.x;
        backdropRect.minY = windowPosition.y;
        backdropRect.maxX = windowPosition.x + windowSize.x;
        backdropRect.maxY = windowPosition.y + windowSize.y;
        backdropRect.rounding = rounding;
        backdropRect.cornerMask = UiBackdropCornersAll;
        backdropRect.composite = true;
        backdropRect.visible =
            windowSize.x > 0.f &&
            windowSize.y > 0.f;
    }

auto UIRenderer::CapturePanelSurfaceBackdrops(
        UiBackdropRect& titleBackdrop,
        UiBackdropRect& bodyBackdrop,
        const ImVec2& windowPosition,
        const ImVec2& windowSize,
        float titleHeight,
        bool expanded,
        float titleRounding,
        float bodyRounding) -> void {
        constexpr float SurfaceInset = 0.5f;
        const float minimumX = windowPosition.x + SurfaceInset;
        const float maximumX =
            windowPosition.x + windowSize.x - SurfaceInset;
        const float minimumY = windowPosition.y + SurfaceInset;
        const float maximumY =
            windowPosition.y + windowSize.y - SurfaceInset;
        const float titleMaximumY = std::clamp(
            windowPosition.y + titleHeight,
            minimumY,
            maximumY);

        titleBackdrop.minX = minimumX;
        titleBackdrop.minY = minimumY;
        titleBackdrop.maxX = maximumX;
        titleBackdrop.maxY = titleMaximumY;
        titleBackdrop.rounding = titleRounding;
        titleBackdrop.cornerMask = UiBackdropCornersAll;
        titleBackdrop.opacity = 1.f;
        titleBackdrop.shadowBlur = 0.f;
        titleBackdrop.shadowOpacity = 0.f;
        titleBackdrop.shadowOffsetY = 0.f;
        titleBackdrop.composite = true;
        titleBackdrop.visible =
            titleBackdrop.maxX > titleBackdrop.minX &&
            titleBackdrop.maxY > titleBackdrop.minY;

        bodyBackdrop.minX = minimumX;
        bodyBackdrop.minY = titleMaximumY;
        bodyBackdrop.maxX = maximumX;
        bodyBackdrop.maxY = maximumY;
        bodyBackdrop.rounding = bodyRounding;
        bodyBackdrop.cornerMask = UiBackdropCornersAll;
        bodyBackdrop.opacity = 1.f;
        bodyBackdrop.shadowBlur = 0.f;
        bodyBackdrop.shadowOpacity = 0.f;
        bodyBackdrop.shadowOffsetY = 0.f;
        bodyBackdrop.composite = true;
        bodyBackdrop.visible = expanded &&
            bodyBackdrop.maxX > bodyBackdrop.minX &&
            bodyBackdrop.maxY > bodyBackdrop.minY + 0.5f;
    }

auto UIRenderer::ApplyWindowAppearance(
        ImDrawList* drawList,
        const ImVec2& pivot,
        float scale,
        float opacity) -> void {
        if (!drawList)
            return;

        const float clampedScale = std::clamp(scale, 0.f, 1.f);
        const float clampedOpacity = std::clamp(opacity, 0.f, 1.f);
        if (clampedScale >= 1.f && clampedOpacity >= 1.f)
            return;

        for (ImDrawVert& vertex : drawList->VtxBuffer)
        {
            vertex.pos = ImVec2(
                pivot.x + (vertex.pos.x - pivot.x) * clampedScale,
                pivot.y + (vertex.pos.y - pivot.y) * clampedScale);
            const uint32_t alpha = (vertex.col >> 24u) & 0xffu;
            const uint32_t fadedAlpha = static_cast<uint32_t>(
                std::round(float(alpha) * clampedOpacity));
            vertex.col =
                (vertex.col & 0x00ffffffu) |
                (fadedAlpha << 24u);
        }
        for (ImDrawCmd& command : drawList->CmdBuffer)
        {
            command.ClipRect = ImVec4(
                pivot.x +
                    (command.ClipRect.x - pivot.x) * clampedScale,
                pivot.y +
                    (command.ClipRect.y - pivot.y) * clampedScale,
                pivot.x +
                    (command.ClipRect.z - pivot.x) * clampedScale,
                pivot.y +
                    (command.ClipRect.w - pivot.y) * clampedScale);
        }
    }

auto UIRenderer::ApplyBackdropAppearance(
        UiBackdropRect& backdropRect,
        const ImVec2& pivot,
        float scale,
        float opacity) -> void {
        const float clampedScale = std::clamp(scale, 0.f, 1.f);
        backdropRect.minX =
            pivot.x + (backdropRect.minX - pivot.x) * clampedScale;
        backdropRect.minY =
            pivot.y + (backdropRect.minY - pivot.y) * clampedScale;
        backdropRect.maxX =
            pivot.x + (backdropRect.maxX - pivot.x) * clampedScale;
        backdropRect.maxY =
            pivot.y + (backdropRect.maxY - pivot.y) * clampedScale;
        for (UiBackdropExclusionRect& exclusion :
            backdropRect.compositeExclusions)
        {
            exclusion.minX =
                pivot.x + (exclusion.minX - pivot.x) * clampedScale;
            exclusion.minY =
                pivot.y + (exclusion.minY - pivot.y) * clampedScale;
            exclusion.maxX =
                pivot.x + (exclusion.maxX - pivot.x) * clampedScale;
            exclusion.maxY =
                pivot.y + (exclusion.maxY - pivot.y) * clampedScale;
        }
        backdropRect.rounding *= clampedScale;
        backdropRect.opacity = std::clamp(opacity, 0.f, 1.f);
    }

auto UIRenderer::CompositeUiColorOver(
        const ImVec4& foreground,
        const ImVec4& background) -> ImVec4 {
        const float foregroundAlpha =
            std::clamp(foreground.w, 0.f, 1.f);
        const float backgroundAlpha =
            std::clamp(background.w, 0.f, 1.f);
        const float outputAlpha =
            foregroundAlpha +
            backgroundAlpha * (1.f - foregroundAlpha);
        if (outputAlpha <= 0.f)
            return ImVec4(0.f, 0.f, 0.f, 0.f);

        const float backgroundContribution =
            backgroundAlpha * (1.f - foregroundAlpha);
        return ImVec4(
            (foreground.x * foregroundAlpha +
                background.x * backgroundContribution) /
                outputAlpha,
            (foreground.y * foregroundAlpha +
                background.y * backgroundContribution) /
                outputAlpha,
            (foreground.z * foregroundAlpha +
                background.z * backgroundContribution) /
                outputAlpha,
            outputAlpha);
    }

auto UIRenderer::MakeUiColor(
        const UiRgbaColor& color,
        float alphaMultiplier ) -> ImVec4 {
        return ImVec4(
            std::clamp(color.red, 0.f, 1.f),
            std::clamp(color.green, 0.f, 1.f),
            std::clamp(color.blue, 0.f, 1.f),
            std::clamp(color.alpha * alphaMultiplier, 0.f, 1.f));
    }

auto UIRenderer::ScaleUiColor(
        const UiRgbaColor& color,
        float scale,
        float alphaMultiplier ) -> ImVec4 {
        return ImVec4(
            std::clamp(color.red * scale, 0.f, 1.f),
            std::clamp(color.green * scale, 0.f, 1.f),
            std::clamp(color.blue * scale, 0.f, 1.f),
            std::clamp(color.alpha * alphaMultiplier, 0.f, 1.f));
    }

auto UIRenderer::OffsetUiColor(
        const UiRgbaColor& color,
        float offset,
        float alphaMultiplier ) -> ImVec4 {
        return ImVec4(
            std::clamp(color.red + offset, 0.f, 1.f),
            std::clamp(color.green + offset, 0.f, 1.f),
            std::clamp(color.blue + offset, 0.f, 1.f),
            std::clamp(color.alpha * alphaMultiplier, 0.f, 1.f));
    }

auto UIRenderer::IsUltraBrightUiColor(const UiRgbaColor& color) -> bool {
        const float luminance =
            color.red * 0.2126f +
            color.green * 0.7152f +
            color.blue * 0.0722f;
        return luminance >= 0.68f;
    }

auto UIRenderer::ApplyUiSkin(
        UiSkin skin,
        const UiAccentSettings& accents,
        bool animationsEnabled,
        float displayScale) -> void {
        const UiSkin resolvedSkin = skin == UiSkin::Count
            ? UiSkin::Amp
            : skin;
        const UiSkinPalette* storedPalette =
            FindUiSkinPalette(accents, resolvedSkin);
        const UiSkinPalette* defaultPalette =
            FindDefaultUiSkinPalette(resolvedSkin);
        const UiSkinPalette& palette = storedPalette
            ? *storedPalette
            : defaultPalette
                ? *defaultPalette
                : DefaultUiAmpPalette;
        const bool authoredSkin = resolvedSkin != UiSkin::Og;
        const bool brightPrimaryAccent = authoredSkin &&
            IsUltraBrightUiColor(palette.primaryAccent);
        const bool brightPrimaryBackground = authoredSkin &&
            IsUltraBrightUiColor(palette.primaryBackground);
        const bool sceneTranslucentHeaders = authoredSkin &&
            brightPrimaryAccent;

        ImGuiStyle style;
        ImGui::StyleColorsDark(&style);
        ImVec4* colors = style.Colors;
        UiVisualTokens tokens;
        tokens.errorText = MakeUiColor(accents.secondaryAccent);
        tokens.successText = MakeUiColor(accents.tertiaryAccent);

        if (!authoredSkin)
        {
            style.ScrollbarRounding = 0.f;
            tokens.drawerHeader = colors[ImGuiCol_Header];
            tokens.drawerHeaderHovered =
                colors[ImGuiCol_HeaderHovered];
            tokens.drawerHeaderActive =
                colors[ImGuiCol_HeaderActive];
            tokens.drawerHeaderText = colors[ImGuiCol_Text];
            tokens.drawerBackground = colors[ImGuiCol_ChildBg];
            tokens.drawerFrame = colors[ImGuiCol_FrameBg];
            tokens.drawerFrameHovered =
                colors[ImGuiCol_FrameBgHovered];
            tokens.drawerFrameActive =
                colors[ImGuiCol_FrameBgActive];
            tokens.outlineTop = colors[ImGuiCol_Border];
            tokens.outlineBottom = colors[ImGuiCol_Border];
            tokens.panelBodySurface = colors[ImGuiCol_WindowBg];
            tokens.colorPickerSurface = colors[ImGuiCol_PopupBg];
            tokens.panelInsetFrame = ImVec4(
                colors[ImGuiCol_WindowBg].x,
                colors[ImGuiCol_WindowBg].y,
                colors[ImGuiCol_WindowBg].z,
                1.f);
            tokens.settingsTitleSurface =
                colors[ImGuiCol_TitleBgActive];
            tokens.settingsTitleText = colors[ImGuiCol_Text];
            tokens.actionButton = colors[ImGuiCol_Button];
            tokens.actionButtonHovered =
                colors[ImGuiCol_ButtonHovered];
            tokens.actionButtonActive =
                colors[ImGuiCol_ButtonActive];
            tokens.actionButtonText = colors[ImGuiCol_Text];
            tokens.drawerRounding = style.ChildRounding;
            tokens.drawControlOutlines = false;
        }
        else
        {
            constexpr float SecondaryRestAlpha = 0.72f;
            constexpr float AuthoredCornerRounding = 4.f;
            style.WindowRounding = AuthoredCornerRounding;
            style.ChildRounding = AuthoredCornerRounding;
            style.PopupRounding = AuthoredCornerRounding;
            style.FrameRounding = AuthoredCornerRounding;
            style.GrabRounding = AuthoredCornerRounding;
            style.ScrollbarRounding = AuthoredCornerRounding;
            style.ScrollbarSize = 12.f;
            style.TabRounding = AuthoredCornerRounding;
            style.WindowBorderSize = 1.f;
            style.DisabledAlpha = 0.38f;

            colors[ImGuiCol_Text] = MakeUiColor(palette.fontColor);
            colors[ImGuiCol_TextDisabled] =
                ScaleUiColor(palette.fontColor, 0.62f);
            colors[ImGuiCol_WindowBg] = MakeUiColor(
                palette.primaryBackground,
                0.60f / SecondaryRestAlpha);
            colors[ImGuiCol_ChildBg] =
                ImVec4(0.f, 0.f, 0.f, 0.f);
            colors[ImGuiCol_PopupBg] = MakeUiColor(
                palette.primaryBackground,
                0.92f / SecondaryRestAlpha);
            colors[ImGuiCol_Border] =
                ImVec4(0.15f, 0.15f, 0.15f, 0.92f);
            colors[ImGuiCol_BorderShadow] =
                ImVec4(0.01f, 0.012f, 0.016f, 0.48f);
            colors[ImGuiCol_FrameBg] =
                MakeUiColor(palette.primaryBackground);
            colors[ImGuiCol_FrameBgHovered] = brightPrimaryBackground
                ? ScaleUiColor(
                    palette.primaryBackground,
                    0.82f,
                    0.76f / SecondaryRestAlpha)
                : OffsetUiColor(
                    palette.primaryBackground,
                    0.112f,
                    0.76f / SecondaryRestAlpha);
            colors[ImGuiCol_FrameBgActive] = brightPrimaryBackground
                ? ScaleUiColor(
                    palette.primaryBackground,
                    0.70f,
                    0.82f / SecondaryRestAlpha)
                : OffsetUiColor(
                    palette.primaryBackground,
                    0.162f,
                    0.82f / SecondaryRestAlpha);
            colors[ImGuiCol_TitleBg] =
                colors[ImGuiCol_FrameBg];
            colors[ImGuiCol_TitleBgActive] =
                colors[ImGuiCol_FrameBgHovered];
            colors[ImGuiCol_TitleBgCollapsed] =
                colors[ImGuiCol_FrameBg];
            colors[ImGuiCol_ScrollbarBg] = MakeUiColor(
                palette.primaryBackground,
                0.36f / SecondaryRestAlpha);
            colors[ImGuiCol_ScrollbarGrab] =
                ImVec4(0.66f, 0.67f, 0.69f, 0.13f);
            colors[ImGuiCol_ScrollbarGrabHovered] =
                ImVec4(0.74f, 0.75f, 0.77f, 0.20f);
            colors[ImGuiCol_ScrollbarGrabActive] =
                ImVec4(0.80f, 0.81f, 0.83f, 0.26f);
            const ImVec4 opaquePrimaryAccent(
                palette.primaryAccent.red,
                palette.primaryAccent.green,
                palette.primaryAccent.blue,
                1.f);
            colors[ImGuiCol_CheckMark] = opaquePrimaryAccent;
            colors[ImGuiCol_Button] = colors[ImGuiCol_FrameBg];
            colors[ImGuiCol_ButtonHovered] =
                colors[ImGuiCol_FrameBgHovered];
            colors[ImGuiCol_ButtonActive] =
                colors[ImGuiCol_FrameBgActive];
            colors[ImGuiCol_ResizeGrip] =
                ImVec4(0.48f, 0.49f, 0.51f, 0.28f);
            colors[ImGuiCol_ResizeGripHovered] =
                ImVec4(0.60f, 0.61f, 0.63f, 0.62f);
            colors[ImGuiCol_ResizeGripActive] =
                ImVec4(0.75f, 0.76f, 0.78f, 0.90f);

            tokens.drawerHeader =
                MakeUiColor(palette.primaryAccent);
            if (brightPrimaryAccent)
            {
                // Ultra-bright custom Amp accents darken on interaction while
                // retaining Amp's authored opacity curve.
                tokens.drawerHeaderHovered = ScaleUiColor(
                    palette.primaryAccent,
                    0.82f,
                    0.48f / 0.31f);
                tokens.drawerHeaderActive = ScaleUiColor(
                    palette.primaryAccent,
                    0.70f,
                    0.65f / 0.31f);
            }
            else
            {
                tokens.drawerHeaderHovered = MakeUiColor(
                    palette.primaryAccent,
                    0.48f / 0.31f);
                tokens.drawerHeaderActive = MakeUiColor(
                    palette.primaryAccent,
                    0.65f / 0.31f);
            }
            tokens.drawerHeaderText =
                MakeUiColor(palette.fontColor);
            tokens.drawerBackground =
                ImVec4(
                    0.66f,
                    0.67f,
                    0.69f,
                    std::clamp(
                        palette.primaryBackground.alpha *
                            (0.13f / SecondaryRestAlpha),
                        0.f,
                        1.f));
            tokens.drawerFrame = colors[ImGuiCol_FrameBg];
            tokens.drawerFrameHovered =
                colors[ImGuiCol_FrameBgHovered];
            tokens.drawerFrameActive =
                colors[ImGuiCol_FrameBgActive];
            tokens.outlineTop =
                ImVec4(0.005f, 0.006f, 0.008f, 0.14f);
            tokens.outlineBottom =
                ImVec4(0.88f, 0.90f, 0.94f, 0.070f);
            tokens.panelBodySurface = MakeUiColor(
                palette.primaryBackground,
                0.92f / SecondaryRestAlpha);
            tokens.colorPickerSurface = tokens.panelBodySurface;
            tokens.panelInsetFrame = ImVec4(
                tokens.panelBodySurface.x,
                tokens.panelBodySurface.y,
                tokens.panelBodySurface.z,
                1.f);
            tokens.settingsTitleSurface = sceneTranslucentHeaders
                ? tokens.drawerHeader
                : CompositeUiColorOver(
                    tokens.drawerHeader,
                    tokens.panelBodySurface);
            tokens.settingsTitleText =
                MakeUiColor(palette.fontColor);
            tokens.actionButton = tokens.drawerHeader;
            tokens.actionButtonHovered =
                tokens.drawerHeaderHovered;
            tokens.actionButtonActive =
                tokens.drawerHeaderActive;
            tokens.actionButtonText =
                MakeUiColor(palette.fontColor);
            tokens.drawerRounding = style.ChildRounding;
            tokens.sceneTranslucentHeaders =
                sceneTranslucentHeaders;
            colors[ImGuiCol_Header] = tokens.drawerHeader;
            colors[ImGuiCol_HeaderHovered] =
                tokens.drawerHeaderHovered;
            colors[ImGuiCol_HeaderActive] =
                tokens.drawerHeaderActive;
            colors[ImGuiCol_SliderGrab] =
                tokens.drawerHeader;
            colors[ImGuiCol_SliderGrabActive] =
                tokens.drawerHeaderActive;
        }

        const UiSkinBehavior behavior =
            GetUiSkinBehavior(resolvedSkin);
        ImGui::SetUvsrUiBehavior(
            ResolveUiMotionEnabled(
                resolvedSkin,
                animationsEnabled),
            behavior.stockImGuiWidgets,
            tokens.sceneTranslucentHeaders);
        ImGui::SetUvsrUiAccentColors(
            MakeUiColor(accents.secondaryAccent),
            MakeUiColor(accents.tertiaryAccent));
        ImGui::SetUvsrSliderTrackColors(
            tokens.drawerFrame,
            tokens.drawerFrameHovered,
            tokens.drawerFrameActive);
        const float safeDisplayScale =
            std::clamp(displayScale, 0.5f, 4.f);
        style.ScaleAllSizes(safeDisplayScale);
        const UiSpacingTokens spacing =
            ResolveUiSpacingTokens(safeDisplayScale);
        style.WindowPadding =
            ImVec2(spacing.regular, spacing.regular);
        ImGui::SetUvsrAuthoredWindowPadding(style.WindowPadding);
        style.ItemSpacing =
            ImVec2(spacing.regular, spacing.tight);
        style.ItemInnerSpacing =
            ImVec2(spacing.tight, spacing.tight);
        tokens.drawerRounding *= safeDisplayScale;
        if (authoredSkin)
        {
            // Keep the authored edge stroke physically thin while radii scale
            // with the rest of the controls instead of sharpening at high DPI.
            style.WindowBorderSize = 1.f;
            style.CircleTessellationMaxError = 0.20f;
        }
        tokens.controlDisabledAlpha = style.DisabledAlpha;
        g_UiSpacingTokens = spacing;
        g_UiVisualTokens = tokens;
        ImGui::GetStyle() = style;
    }

auto UIRenderer::PushPanelBodySurface() -> void {
        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            g_UiVisualTokens.panelBodySurface);
    }

auto UIRenderer::GetOpaquePanelBodySurface() -> ImVec4 {
        ImVec4 surface = g_UiVisualTokens.panelBodySurface;
        surface.w = 1.f;
        return surface;
    }

auto UIRenderer::PushOpaquePanelBodySurface() -> void {
        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            GetOpaquePanelBodySurface());
    }

auto UIRenderer::GetUiLayoutAnimationStep() -> float {
        const float animationDeltaTime = std::min(
            std::max(0.f, ImGui::GetIO().DeltaTime),
            1.f / 30.f);
        return std::min(
            1.f,
            animationDeltaTime /
                UiLayoutAnimationDurationSeconds);
    }

auto UIRenderer::GetCommandInterfaceMinimumHeight() -> float {
        const ImGuiStyle& style = ImGui::GetStyle();
        return std::ceil(
            style.WindowPadding.y * 2.f +
            ImGui::GetFrameHeight());
    }

auto UIRenderer::GetCommandInterfaceReservedHeight() -> float {
        return GetCommandInterfaceMinimumHeight();
    }

auto UIRenderer::GetPanelTitleHeight(
        const ImGuiStyle& style,
        float fontSize) -> float {
        return fontSize + style.FramePadding.y * 2.f;
    }

auto UIRenderer::GetSettingsCollapsedWindowHeight(
        const ImGuiStyle& style,
        float fontSize) -> float {
        return GetPanelTitleHeight(style, fontSize) +
            style.WindowPadding.y * 2.f +
            fontSize +
            g_UiSpacingTokens.tight;
    }

auto UIRenderer::GetSettingsMinimumExpandedWindowHeight(
        const ImGuiStyle& style,
        float fontSize) -> float {
        return GetSettingsCollapsedWindowHeight(style, fontSize) +
            fontSize +
            g_UiSpacingTokens.tight;
    }

auto UIRenderer::AdvanceUiLayoutAnimation(
        float amount,
        bool targetVisible) -> float {
        if (!ImGui::IsUvsrUiMotionEnabled())
            return targetVisible ? 1.f : 0.f;

        const float step = GetUiLayoutAnimationStep();
        return targetVisible
            ? std::min(1.f, amount + step)
            : std::max(0.f, amount - step);
    }

auto UIRenderer::SmoothUiLayoutAnimation(float linearAmount) -> float {
        const float amount = std::clamp(linearAmount, 0.f, 1.f);
        return amount * amount * (3.f - 2.f * amount);
    }

auto UIRenderer::PrepareSettingsScrollStability() -> void {
        SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        if (context.lastFrame < ImGui::GetFrameCount() - 1)
        {
            context.layoutAnimatingLastFrame = false;
            context.retainedViewportHeight = 0.f;
            context.lastScrollY = 0.f;
        }
    }

auto UIRenderer::GetSettingsBodyMinimumHeight(
        float maximumHeight) -> float {
        const SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        const bool holdPreviousHeight =
            ShouldRetainUiViewportHeight(
                context.lastScrollY > 0.5f,
                std::abs(ImGui::GetIO().MouseWheel) > 0.001f,
                ImGui::IsMouseDragging(ImGuiMouseButton_Left));
        return holdPreviousHeight
            ? std::clamp(
                context.retainedViewportHeight,
                0.f,
                maximumHeight)
            : 0.f;
    }

auto UIRenderer::MarkSettingsLayoutAnimationActive() -> void {
        g_SettingsScrollStabilityContext
            .layoutAnimatingThisFrame = true;
    }

auto UIRenderer::EnsureAnimatedChildLayoutSubmission(
        bool& bodySubmitted) -> void {
        if (bodySubmitted)
            return;

        // BeginChild normally skips a fully clipped child. Animated Settings
        // bodies still need their logical layout submitted while offscreen:
        // otherwise TreeNodeEx reports false, nested presentation state closes,
        // cached heights become stale, and returning to the drawer can shift
        // the viewport. Item-level clipping still prevents draw work.
        ImGui::GetCurrentWindow()->SkipItems = false;
        bodySubmitted = true;
    }

auto UIRenderer::BeginSettingsScrollStability() -> void {
        SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        const int frame = ImGui::GetFrameCount();
        if (context.lastFrame < frame - 1)
            context.previousAnchors.clear();

        context.active = true;
        context.scrollY = ImGui::GetScrollY();
        const float scrollMaxY = ImGui::GetScrollMaxY();
        const ImGuiContext* imguiContext =
            ImGui::GetCurrentContext();
        const bool settingsBodyConsumedWheel =
            imguiContext &&
            imguiContext->WheelingWindow ==
                ImGui::GetCurrentWindow() &&
            imguiContext->WheelingWindowScrolledFrame == frame;
        context.wheelInput = settingsBodyConsumedWheel
            ? ImGui::GetIO().MouseWheel
            : 0.f;
        context.wheelAtTop = context.scrollY <= 0.5f;
        context.wheelAtBottom =
            scrollMaxY > 0.5f &&
            scrollMaxY - context.scrollY <=
                std::max(1.f, ImGui::GetFrameHeight() * 0.5f);
        context.preserveBottom =
            context.wheelAtBottom;
        context.viewportTopScreenY =
            ImGui::GetCursorScreenPos().y + context.scrollY;
        context.layoutAnimatingThisFrame = false;
        context.drawerHeightDeltas = {};
        context.currentAnchors.clear();
        context.translucentHeaderSupportRects.clear();
        context.rootDrawList = ImGui::GetWindowDrawList();
        context.rootDrawVertexStart =
            context.rootDrawList
                ? context.rootDrawList->VtxBuffer.Size
                : 0;
        context.lastFrame = frame;
    }

auto UIRenderer::TrackSettingsScrollAnchor(
        ImGuiID id,
        float screenY) -> void {
        SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        if (!context.active || id == 0)
            return;

        const auto duplicate = std::find_if(
            context.currentAnchors.begin(),
            context.currentAnchors.end(),
            [id](const SettingsScrollAnchorPosition& anchor)
            {
                return anchor.id == id;
            });
        if (duplicate != context.currentAnchors.end())
            return;

        context.currentAnchors.push_back({
            id,
            screenY - context.viewportTopScreenY +
                context.scrollY
        });
    }

auto UIRenderer::TrackSettingsDrawerHeight(
        ImGuiStorage* storage,
        ImGuiID headerId,
        float bodyTop,
        float displayedHeight) -> void {
        if (!storage || headerId == 0)
            return;

        const ImGuiID displayedHeightKey =
            headerId ^ ImGuiID(0x786A4D21u);
        const float previousDisplayedHeight =
            storage->GetFloat(
                displayedHeightKey,
                displayedHeight);
        SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        if (context.active)
        {
            context.drawerHeightDeltas =
                AccumulateUiDrawerHeightDelta(
                    context.drawerHeightDeltas,
                    bodyTop,
                    previousDisplayedHeight,
                    displayedHeight,
                    context.viewportTopScreenY);
        }
        storage->SetFloat(
            displayedHeightKey,
            displayedHeight);
    }

auto UIRenderer::EndSettingsScrollStability() -> void {
        SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        if (!context.active)
            return;

        bool foundStableAnchor = false;
        float scrollDelta = 0.f;
        for (const SettingsScrollAnchorPosition& previous :
            context.previousAnchors)
        {
            if (previous.contentY < context.scrollY - 0.5f)
                continue;

            const auto current = std::find_if(
                context.currentAnchors.begin(),
                context.currentAnchors.end(),
                [&](const SettingsScrollAnchorPosition& anchor)
                {
                    return anchor.id == previous.id;
                });
            if (current == context.currentAnchors.end())
                continue;

            scrollDelta = current->contentY - previous.contentY;
            foundStableAnchor = true;
            break;
        }

        if (!foundStableAnchor)
        {
            scrollDelta = ResolveUiScrollAnchorDelta(
                context.drawerHeightDeltas,
                context.preserveBottom);
        }

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const float currentFrameContentHeight =
            window->ContentSizeExplicit.y != 0.f
                ? window->ContentSizeExplicit.y
                : std::trunc(
                    window->DC.CursorMaxPos.y -
                    window->DC.CursorStartPos.y);
        const float currentFrameScrollMaxY = std::max(
            0.f,
            currentFrameContentHeight +
                window->WindowPadding.y * 2.f -
                window->InnerRect.GetHeight());
        const UiScrollAnchorCorrection correction =
            ResolveUiScrollAnchorCorrection(
                window->Scroll.y,
                scrollDelta,
                currentFrameScrollMaxY,
                window->ScrollTarget.y < FLT_MAX,
                context.wheelInput,
                context.wheelAtTop,
                context.wheelAtBottom);
        if (correction.apply)
        {
            const float visualScrollDelta =
                correction.scrollY - window->Scroll.y;
            window->Scroll.y = correction.scrollY;
            if (std::abs(visualScrollDelta) > 0.01f)
            {
                for (ImDrawList* drawList :
                    g_SettingsAppearanceDrawLists)
                {
                    if (!drawList)
                        continue;
                    const int vertexStart =
                        drawList == context.rootDrawList
                            ? std::clamp(
                                context.rootDrawVertexStart,
                                0,
                                drawList->VtxBuffer.Size)
                            : 0;
                    for (int vertexIndex = vertexStart;
                        vertexIndex < drawList->VtxBuffer.Size;
                        ++vertexIndex)
                    {
                        drawList->VtxBuffer[vertexIndex].pos.y -=
                            visualScrollDelta;
                    }

                    if (drawList == context.rootDrawList)
                        continue;
                    for (ImDrawCmd& command :
                        drawList->CmdBuffer)
                    {
                        command.ClipRect.y = std::max(
                            window->InnerClipRect.Min.y,
                            command.ClipRect.y -
                                visualScrollDelta);
                        command.ClipRect.w = std::min(
                            window->InnerClipRect.Max.y,
                            command.ClipRect.w -
                                visualScrollDelta);
                        command.ClipRect.w = std::max(
                            command.ClipRect.y,
                            command.ClipRect.w);
                    }
                }
                for (ImRect& headerRect :
                    context.translucentHeaderSupportRects)
                {
                    headerRect.Min.y -= visualScrollDelta;
                    headerRect.Max.y -= visualScrollDelta;
                }
            }
        }

        context.previousAnchors =
            std::move(context.currentAnchors);
        context.currentAnchors.clear();
        context.retainedViewportHeight =
            ImGui::GetWindowSize().y;
        context.lastScrollY = ImGui::GetScrollY();
        context.layoutAnimatingLastFrame =
            context.layoutAnimatingThisFrame;
        context.active = false;
    }

auto UIRenderer::DrawCollapsingHeader(
        const char* label,
        const char* tooltip,
        ImGuiTreeNodeFlags flags ,
        bool forceClosedPresentation ) -> bool {
        const ImGuiID headerId = ImGui::GetID(label);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID amountKey =
            headerId ^ ImGuiID(0x4A9D31E7u);
        const ImGuiID frameKey =
            headerId ^ ImGuiID(0x71C6B42Du);
        const ImGuiID targetKey =
            headerId ^ ImGuiID(0x2F63C8B5u);
        const ImGuiID measuredHeightKey =
            headerId ^ ImGuiID(0xD14F83A9u);
        const ImGuiID measurementValidKey =
            headerId ^ ImGuiID(0x82E4C76Bu);
        ImGui::PushStyleColor(
            ImGuiCol_Header,
            g_UiVisualTokens.drawerHeader);
        ImGui::PushStyleColor(
            ImGuiCol_HeaderHovered,
            g_UiVisualTokens.drawerHeaderHovered);
        ImGui::PushStyleColor(
            ImGuiCol_HeaderActive,
            g_UiVisualTokens.drawerHeaderActive);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            g_UiVisualTokens.drawerHeaderText);
        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            ImGui::GetStyle().FrameRounding);
        const bool useAuthoredHeaderFont =
            m_ComposedUiSkin != UiSkin::Og;
        if (useAuthoredHeaderFont)
        {
            ImGui::PushFont(GetActiveUiHeaderFont());
            ApplyActiveUiHeaderWordSpacing();
        }
        ImGuiStyle& style = ImGui::GetStyle();
        const float itemSpacingY = style.ItemSpacing.y;
        style.ItemSpacing.y = 0.f;
        ImGui::BeginUvsrTreeArrowCapture();
        const bool open = ImGui::CollapsingHeader(label, flags);
        style.ItemSpacing.y = itemSpacingY;
        if (useAuthoredHeaderFont)
        {
            RestoreActiveUiHeaderWordSpacing();
            ImGui::PopFont();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        if (g_UiVisualTokens.sceneTranslucentHeaders &&
            g_SettingsScrollStabilityContext.active)
        {
            g_SettingsScrollStabilityContext
                .translucentHeaderSupportRects.push_back(
                    ImRect(
                        ImGui::GetItemRectMin(),
                        ImGui::GetItemRectMax()));
        }
        ImGui::SetItemTooltip(tooltip);
        TrackSettingsScrollAnchor(
            headerId,
            ImGui::GetItemRectMin().y);

        const int frame = ImGui::GetFrameCount();
        const int lastFrame = forceClosedPresentation
            ? frame - 2
            : storage->GetInt(frameKey, -2);
        const bool previousTargetOpen = forceClosedPresentation
            ? false
            : storage->GetBool(targetKey, open);
        float openAmount = forceClosedPresentation
            ? 0.f
            : storage->GetFloat(
                amountKey,
                open ? 1.f : 0.f);
        const UiExpandedMeasurementState measurement = {
            storage->GetFloat(measuredHeightKey, 0.f),
            storage->GetBool(measurementValidKey, false)
        };
        const float measuredHeight = measurement.height;
        const bool needsInitialMeasurement =
            ImGui::IsUvsrUiMotionEnabled() &&
            NeedsInitialUiExpandedMeasurement(open, measurement);
        if (!ImGui::IsUvsrUiMotionEnabled())
        {
            openAmount = open ? 1.f : 0.f;
        }
        else if (lastFrame < frame - 1)
        {
            openAmount = ResolveUiOpenAmountAfterSubmissionGap(
                open,
                previousTargetOpen,
                needsInitialMeasurement);
        }
        else if (needsInitialMeasurement)
        {
            // Submit one alpha-zero layout pass before visible progress. This
            // gives every drawer a real expanded height instead of animating
            // from a one-row proxy.
            openAmount = 0.f;
        }
        else
        {
            openAmount =
                AdvanceUiLayoutAnimation(openAmount, open);
        }
        storage->SetFloat(amountKey, openAmount);
        storage->SetInt(frameKey, frame);
        storage->SetBool(targetKey, open);
        ImGui::EndUvsrTreeArrowCapture(
            SmoothUiLayoutAnimation(openAmount),
            (flags & ImGuiTreeNodeFlags_UpsideDownArrow) != 0);
        if (needsInitialMeasurement ||
            (openAmount > 0.f && openAmount < 1.f))
        {
            MarkSettingsLayoutAnimationActive();
        }
        g_DrawerAnimationContext = {
            storage,
            headerId,
            openAmount,
            open,
            needsInitialMeasurement,
            false
        };
        const bool drawBody = open || openAmount > 0.f;
        if (!drawBody)
        {
            TrackSettingsDrawerHeight(
                storage,
                headerId,
                ImGui::GetItemRectMax().y,
                0.f);
        }
        return drawBody;
    }

auto UIRenderer::BeginDrawerBody(
        const char* id,
        float controlWidth,
        float maximumHeight ) -> void {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiID measuredHeightKey =
            g_DrawerAnimationContext.headerId ^
            ImGuiID(0xD14F83A9u);
        const float measuredHeight =
            g_DrawerAnimationContext.storage != nullptr
                ? g_DrawerAnimationContext.storage->GetFloat(
                    measuredHeightKey,
                    0.f)
                : 0.f;
        const bool motionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        const float easedAmount = motionEnabled
            ? SmoothUiLayoutAnimation(
                g_DrawerAnimationContext.openAmount)
            : g_DrawerAnimationContext.targetOpen ? 1.f : 0.f;
        const bool scrollableBody = maximumHeight > 0.f;
        const float uncappedAnimatedHeight =
            !motionEnabled
                ? 0.f
                : g_DrawerAnimationContext.needsInitialMeasurement
                ? 0.001f
                : std::max(
                    measuredHeight * easedAmount,
                    0.001f);
        const float animatedHeight = scrollableBody && motionEnabled
            ? std::min(uncappedAnimatedHeight, maximumHeight)
            : uncappedAnimatedHeight;
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            g_UiVisualTokens.drawerBackground);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            g_UiVisualTokens.drawerFrame);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgHovered,
            g_UiVisualTokens.drawerFrameHovered);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgActive,
            g_UiVisualTokens.drawerFrameActive);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(
                g_UiSpacingTokens.tight,
                g_UiSpacingTokens.tight));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            g_UiVisualTokens.drawerRounding);
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            style.Alpha *
                (g_DrawerAnimationContext.needsInitialMeasurement
                    ? 0.f
                    : easedAmount));
        ImGuiChildFlags childFlags =
            ImGuiChildFlags_AlwaysUseWindowPadding |
            ImGuiChildFlags_AllowZeroSize;
        if (!motionEnabled)
        {
            childFlags |=
                ImGuiChildFlags_AutoResizeY |
                ImGuiChildFlags_AlwaysAutoResize;
        }
        ImGuiWindowFlags childWindowFlags = ImGuiWindowFlags_None;
        if (!scrollableBody)
        {
            childWindowFlags |=
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
        }
        if (motionEnabled &&
            (g_DrawerAnimationContext.needsInitialMeasurement ||
            !g_DrawerAnimationContext.targetOpen ||
            g_DrawerAnimationContext.openAmount < 1.f))
        {
            childWindowFlags |= ImGuiWindowFlags_NoInputs;
        }
        if (scrollableBody)
        {
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(0.f, 0.f),
                ImVec2(FLT_MAX, maximumHeight));
        }
        g_DrawerAnimationContext.bodyVisible =
            ImGui::BeginChild(
            id,
            ImVec2(0.f, animatedHeight),
            childFlags,
            childWindowFlags);
        TrackSettingsAppearanceDrawList(ImGui::GetWindowDrawList());
        EnsureAnimatedChildLayoutSubmission(
            g_DrawerAnimationContext.bodyVisible);
        ImGui::PushItemWidth(controlWidth);
    }

auto UIRenderer::LerpUiColor(
        const ImVec4& normal,
        const ImVec4& interaction,
        float amount) -> ImVec4 {
        return ImVec4(
            normal.x + (interaction.x - normal.x) * amount,
            normal.y + (interaction.y - normal.y) * amount,
            normal.z + (interaction.z - normal.z) * amount,
            normal.w + (interaction.w - normal.w) * amount);
    }

auto UIRenderer::DrawDrawerBodyOutline(
        ImDrawList* drawList,
        const ImVec2& minimum,
        const ImVec2& maximum,
        float rounding,
        float topGap,
        bool intersectClipRect) -> void {
        if (!drawList ||
            !g_UiVisualTokens.drawControlOutlines)
            return;

        constexpr float Thickness = 1.f;
        constexpr float Inset = Thickness * 0.5f;

        const ImVec2 outlineMinimum(
            minimum.x + Inset,
            minimum.y + Inset);
        const ImVec2 outlineMaximum(
            maximum.x - Inset,
            maximum.y - Inset);
        const float width = outlineMaximum.x - outlineMinimum.x;
        const float height = outlineMaximum.y - outlineMinimum.y;
        if (width <= Thickness || height <= topGap + Thickness)
            return;

        const float clipTop = topGap > 0.f
            ? outlineMinimum.y + topGap
            : outlineMinimum.y - Thickness;
        drawList->PushClipRect(
            ImVec2(
                outlineMinimum.x - Thickness,
                clipTop),
            ImVec2(
                outlineMaximum.x + Thickness,
                outlineMaximum.y + Thickness),
            intersectClipRect);
        const int vertexStart = drawList->VtxBuffer.Size;
        drawList->AddRect(
            outlineMinimum,
            outlineMaximum,
            IM_COL32_WHITE,
            std::max(0.f, rounding - Inset),
            ImDrawFlags_RoundCornersAll,
            Thickness);
        const int vertexEnd = drawList->VtxBuffer.Size;
        drawList->PopClipRect();

        const float gradientExtent = std::max(height, 1.f);
        for (int vertexIndex = vertexStart;
            vertexIndex < vertexEnd;
            ++vertexIndex)
        {
            ImDrawVert& vertex = drawList->VtxBuffer[vertexIndex];
            const float coverage =
                float((vertex.col & IM_COL32_A_MASK) >>
                    IM_COL32_A_SHIFT) / 255.f;
            const float gradientPosition = std::clamp(
                (vertex.pos.y - outlineMinimum.y) / gradientExtent,
                0.f,
                1.f);
            ImVec4 outlineColor = LerpUiColor(
                g_UiVisualTokens.outlineTop,
                g_UiVisualTokens.outlineBottom,
                gradientPosition);
            outlineColor.w *= coverage;
            vertex.col = ImGui::GetColorU32(outlineColor);
        }
    }

auto UIRenderer::ResolveRoundedRectRadius(
        const ImRect& rectangle,
        float requestedRadius) -> float {
        return std::max(
            0.f,
            std::min({
                requestedRadius,
                rectangle.GetWidth() * 0.5f - 1.f,
                rectangle.GetHeight() * 0.5f - 1.f
            }));
    }

auto UIRenderer::DrawFilledRoundedInsetFrame(
        ImDrawList* drawList,
        const ImRect& outerRect,
        const ImRect& innerRect,
        float rounding) -> void {
        if (!drawList ||
            outerRect.GetWidth() <= 1.f ||
            outerRect.GetHeight() <= 1.f ||
            innerRect.Min.x <= outerRect.Min.x ||
            innerRect.Min.y <= outerRect.Min.y ||
            innerRect.Max.x >= outerRect.Max.x ||
            innerRect.Max.y >= outerRect.Max.y ||
            innerRect.GetWidth() <= 1.f ||
            innerRect.GetHeight() <= 1.f)
        {
            return;
        }

        const float outerRadius = ResolveRoundedRectRadius(
            outerRect,
            rounding);
        const float innerRadius = ResolveRoundedRectRadius(
            innerRect,
            rounding);
        const ImU32 frameColor = ImGui::GetColorU32(
            g_UiVisualTokens.panelInsetFrame);
        if ((frameColor & IM_COL32_A_MASK) == 0u)
            return;
        const ImVec2 expandedOuterMinimum(
            outerRect.Min.x - 1.f,
            outerRect.Min.y - 1.f);
        const ImVec2 expandedOuterMaximum(
            outerRect.Max.x + 1.f,
            outerRect.Max.y + 1.f);

        // Draw the opaque exterior silhouette through four disjoint clips.
        // The center is never painted, so already-submitted menu content stays
        // intact. The four corner wedges below restore the rounded interior
        // edge instead of leaving the rejected transparent square gaps.
        drawList->PushClipRect(
            expandedOuterMinimum,
            expandedOuterMaximum,
            false);
        const auto drawOuterSurfaceThrough =
            [&](const ImVec2& minimum, const ImVec2& maximum)
            {
                if (maximum.x <= minimum.x || maximum.y <= minimum.y)
                    return;
                drawList->PushClipRect(minimum, maximum, true);
                drawList->AddRectFilled(
                    outerRect.Min,
                    outerRect.Max,
                    frameColor,
                    outerRadius,
                    ImDrawFlags_RoundCornersAll);
                drawList->PopClipRect();
            };
        drawOuterSurfaceThrough(
            expandedOuterMinimum,
            ImVec2(outerRect.Max.x + 1.f, innerRect.Min.y));
        drawOuterSurfaceThrough(
            ImVec2(outerRect.Min.x - 1.f, innerRect.Max.y),
            expandedOuterMaximum);
        drawOuterSurfaceThrough(
            ImVec2(outerRect.Min.x - 1.f, innerRect.Min.y),
            ImVec2(innerRect.Min.x, innerRect.Max.y));
        drawOuterSurfaceThrough(
            ImVec2(innerRect.Max.x, innerRect.Min.y),
            ImVec2(outerRect.Max.x + 1.f, innerRect.Max.y));

        if (innerRadius > 0.f)
        {
            const int cornerSegments = std::max(
                3,
                drawList->_CalcCircleAutoSegmentCount(innerRadius) / 4);
            const auto drawInnerCornerWedge =
                [&](const ImVec2& squareCorner,
                    const ImVec2& arcCenter,
                    float startAngle,
                    float endAngle)
                {
                    std::vector<ImVec2> points;
                    points.reserve(static_cast<size_t>(cornerSegments) + 2u);
                    points.push_back(squareCorner);
                    for (int segment = 0;
                        segment <= cornerSegments;
                        ++segment)
                    {
                        const float amount =
                            float(segment) / float(cornerSegments);
                        const float angle =
                            startAngle + (endAngle - startAngle) * amount;
                        points.emplace_back(
                            arcCenter.x + std::cos(angle) * innerRadius,
                            arcCenter.y + std::sin(angle) * innerRadius);
                    }
                    float twiceArea = 0.f;
                    for (size_t index = 0;
                        index < points.size();
                        ++index)
                    {
                        const ImVec2& current = points[index];
                        const ImVec2& next =
                            points[(index + 1u) % points.size()];
                        twiceArea +=
                            current.x * next.y - current.y * next.x;
                    }
                    if (twiceArea < 0.f)
                        std::reverse(points.begin(), points.end());
                    drawList->AddConcavePolyFilled(
                        points.data(),
                        static_cast<int>(points.size()),
                        frameColor);
                };
            drawInnerCornerWedge(
                innerRect.Min,
                ImVec2(
                    innerRect.Min.x + innerRadius,
                    innerRect.Min.y + innerRadius),
                -IM_PI * 0.5f,
                -IM_PI);
            drawInnerCornerWedge(
                ImVec2(innerRect.Max.x, innerRect.Min.y),
                ImVec2(
                    innerRect.Max.x - innerRadius,
                    innerRect.Min.y + innerRadius),
                -IM_PI * 0.5f,
                0.f);
            drawInnerCornerWedge(
                innerRect.Max,
                ImVec2(
                    innerRect.Max.x - innerRadius,
                    innerRect.Max.y - innerRadius),
                0.f,
                IM_PI * 0.5f);
            drawInnerCornerWedge(
                ImVec2(innerRect.Min.x, innerRect.Max.y),
                ImVec2(
                    innerRect.Min.x + innerRadius,
                    innerRect.Max.y - innerRadius),
                IM_PI * 0.5f,
                IM_PI);
        }
        drawList->PopClipRect();
    }

auto UIRenderer::DrawOpaqueRootPanelRetainedContent(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& retainedContentRect,
        float rounding) -> void {
        if (!drawList ||
            bodyRect.GetWidth() <= 1.f ||
            bodyRect.GetHeight() <= 1.f ||
            retainedContentRect.GetWidth() <= 1.f ||
            retainedContentRect.GetHeight() <= 1.f)
        {
            return;
        }

        const ImRect clippedContentRect(
            ImVec2(
                std::max(bodyRect.Min.x, retainedContentRect.Min.x),
                std::max(bodyRect.Min.y, retainedContentRect.Min.y)),
            ImVec2(
                std::min(bodyRect.Max.x, retainedContentRect.Max.x),
                std::min(bodyRect.Max.y, retainedContentRect.Max.y)));
        if (clippedContentRect.GetWidth() <= 1.f ||
            clippedContentRect.GetHeight() <= 1.f)
        {
            return;
        }

        drawList->PushClipRect(
            bodyRect.Min,
            bodyRect.Max,
            false);
        drawList->AddRectFilled(
            clippedContentRect.Min,
            clippedContentRect.Max,
            ImGui::GetColorU32(GetOpaquePanelBodySurface()),
            ResolveRoundedRectRadius(clippedContentRect, rounding),
            ImDrawFlags_RoundCornersAll);
        drawList->PopClipRect();
    }

auto UIRenderer::DrawRootPanelBodySurface(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& contentRect,
        const ImRect& retainedContentRect,
        float rounding) -> void {
        DrawFilledRoundedInsetFrame(
            drawList,
            bodyRect,
            contentRect,
            rounding);
        DrawOpaqueRootPanelRetainedContent(
            drawList,
            bodyRect,
            retainedContentRect,
            rounding);
    }

auto UIRenderer::DrawRootPanelBodyOutlines(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& contentRect,
        float rounding) -> void {
        DrawDrawerBodyOutline(
            drawList,
            bodyRect.Min,
            bodyRect.Max,
            rounding,
            0.f,
            false);
        DrawDrawerBodyOutline(
            drawList,
            contentRect.Min,
            contentRect.Max,
            rounding,
            0.f,
            false);
    }

auto UIRenderer::DrawRootPanelBodyChrome(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& contentRect,
        const ImRect& retainedContentRect,
        float rounding) -> void {
        DrawRootPanelBodySurface(
            drawList,
            bodyRect,
            contentRect,
            retainedContentRect,
            rounding);
        DrawRootPanelBodyOutlines(
            drawList,
            bodyRect,
            contentRect,
            rounding);
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
        DrawRootPanelBodyChrome(
            drawList,
            bodyRect,
            contentRect,
            contentRect,
            rounding);
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

auto UIRenderer::EndDrawerBody() -> void {
        const float measuredHeight = std::max(
            1.f,
            ImGui::GetCursorPosY() +
                ImGui::GetStyle().WindowPadding.y);
        ImGui::PopItemWidth();
        ImGuiStyle& style = ImGui::GetStyle();
        const float itemSpacingY = style.ItemSpacing.y;
        style.ItemSpacing.y = 0.f;
        ImGui::EndChild();
        style.ItemSpacing.y = itemSpacingY;
        if (g_DrawerAnimationContext.storage != nullptr)
        {
            const ImGuiID measuredHeightKey =
                g_DrawerAnimationContext.headerId ^
                ImGuiID(0xD14F83A9u);
            const ImGuiID measurementValidKey =
                g_DrawerAnimationContext.headerId ^
                ImGuiID(0x82E4C76Bu);
            UiExpandedMeasurementState measurement = {
                g_DrawerAnimationContext.storage->GetFloat(
                    measuredHeightKey, 0.f),
                g_DrawerAnimationContext.storage->GetBool(
                    measurementValidKey, false)
            };
            const float renderedHeight =
                ImGui::GetItemRectSize().y;
            measurement = SubmitUiExpandedMeasurement(
                measurement,
                measuredHeight,
                g_DrawerAnimationContext.targetOpen,
                g_DrawerAnimationContext.bodyVisible);
            g_DrawerAnimationContext.storage->SetFloat(
                measuredHeightKey,
                measurement.height);
            g_DrawerAnimationContext.storage->SetBool(
                measurementValidKey,
                measurement.valid);
            TrackSettingsDrawerHeight(
                g_DrawerAnimationContext.storage,
                g_DrawerAnimationContext.headerId,
                ImGui::GetItemRectMin().y,
                renderedHeight);
        }
        DrawDrawerBodyOutline(
            ImGui::GetWindowDrawList(),
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            ImGui::GetStyle().ChildRounding,
            2.f,
            true);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);
    }

auto UIRenderer::BeginAnimatedTreeNode(
        const char* label,
        ImGuiTreeNodeFlags flags ,
        const char* tooltip ) -> bool {
        // Item-width stacks belong to an ImGui window. Preserve the drawer's
        // standard control width before entering this animated child so
        // sliders and dropdowns retain identical tracks at every nesting
        // level.
        const float inheritedItemWidth = ImGui::CalcItemWidth();
        const ImGuiID headerId = ImGui::GetID(label);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID amountKey =
            headerId ^ ImGuiID(0x5CB870A3u);
        const ImGuiID frameKey =
            headerId ^ ImGuiID(0x34A1F27Du);
        const ImGuiID targetKey =
            headerId ^ ImGuiID(0x47B2159Cu);
        const ImGuiID measuredHeightKey =
            headerId ^ ImGuiID(0x9D63E418u);
        const ImGuiID measurementValidKey =
            headerId ^ ImGuiID(0xC1A7095Fu);
        ImGui::BeginUvsrTreeArrowCapture();
        const bool open = ImGui::TreeNodeEx(
            label,
            flags | ImGuiTreeNodeFlags_NoTreePushOnOpen);
        if (tooltip != nullptr)
            ImGui::SetItemTooltip("%s", tooltip);
        TrackSettingsScrollAnchor(
            headerId,
            ImGui::GetItemRectMin().y);

        const int frame = ImGui::GetFrameCount();
        const int lastFrame = storage->GetInt(frameKey, -2);
        const bool previousTargetOpen = storage->GetBool(
            targetKey,
            open);
        const UiExpandedMeasurementState measurement = {
            storage->GetFloat(measuredHeightKey, 0.f),
            storage->GetBool(measurementValidKey, false)
        };
        const float measuredHeight = measurement.height;
        const bool motionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        const bool needsInitialMeasurement =
            motionEnabled &&
            NeedsInitialUiExpandedMeasurement(open, measurement);
        float openAmount = storage->GetFloat(
            amountKey,
            open ? 1.f : 0.f);
        if (!ImGui::IsUvsrUiMotionEnabled())
        {
            openAmount = open ? 1.f : 0.f;
        }
        else if (lastFrame < frame - 1)
        {
            openAmount = ResolveUiOpenAmountAfterSubmissionGap(
                open,
                previousTargetOpen,
                needsInitialMeasurement);
        }
        else if (needsInitialMeasurement)
        {
            openAmount = 0.f;
        }
        else
        {
            openAmount =
                AdvanceUiLayoutAnimation(openAmount, open);
        }
        storage->SetFloat(amountKey, openAmount);
        storage->SetInt(frameKey, frame);
        storage->SetBool(targetKey, open);
        ImGui::EndUvsrTreeArrowCapture(
            SmoothUiLayoutAnimation(openAmount),
            (flags & ImGuiTreeNodeFlags_UpsideDownArrow) != 0);
        if (needsInitialMeasurement ||
            (openAmount > 0.f && openAmount < 1.f))
        {
            MarkSettingsLayoutAnimationActive();
        }

        if (!open && openAmount <= 0.f)
            return false;

        const float easedAmount =
            SmoothUiLayoutAnimation(openAmount);
        const float animatedHeight =
            !motionEnabled
                ? 0.f
                : needsInitialMeasurement
                ? 0.001f
                : std::max(
                    measuredHeight * easedAmount,
                    0.001f);

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            0.f);
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha *
                (needsInitialMeasurement
                    ? 0.f
                    : easedAmount));
        ImGuiWindowFlags childWindowFlags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (motionEnabled &&
            (needsInitialMeasurement ||
            !open ||
            openAmount < 1.f))
        {
            childWindowFlags |= ImGuiWindowFlags_NoInputs;
        }
        ImGuiChildFlags childFlags =
            ImGuiChildFlags_AllowZeroSize;
        if (!motionEnabled)
        {
            childFlags |=
                ImGuiChildFlags_AutoResizeY |
                ImGuiChildFlags_AlwaysAutoResize;
        }
        bool bodyVisible = ImGui::BeginChild(
            headerId ^ ImGuiID(0xE60792B5u),
            ImVec2(0.f, animatedHeight),
            childFlags,
            childWindowFlags);
        TrackSettingsAppearanceDrawList(ImGui::GetWindowDrawList());
        EnsureAnimatedChildLayoutSubmission(bodyVisible);
        // Own the transparent indentation gutter inside the animated child so
        // nested-dropdown reset buttons can draw and receive input there. The
        // child starts one indent earlier, while this internal indent preserves
        // every existing control's absolute position and right edge.
        const float indentSpacing = ImGui::GetStyle().IndentSpacing;
        ImGui::Indent(indentSpacing);
        ImGui::PushItemWidth(inheritedItemWidth);
        g_NestedDrawerAnimationContexts.push_back({
            storage,
            ImGui::GetCurrentWindow(),
            measuredHeightKey,
            measurementValidKey,
            indentSpacing,
            open,
            bodyVisible
        });
        return true;
    }

auto UIRenderer::EndAnimatedTreeNode() -> void {
        assert(!g_NestedDrawerAnimationContexts.empty());
        const NestedDrawerAnimationContext context =
            g_NestedDrawerAnimationContexts.back();
        g_NestedDrawerAnimationContexts.pop_back();
        const float measuredHeight =
            std::max(0.f, ImGui::GetCursorPosY());
        ImGuiStyle& style = ImGui::GetStyle();
        const float itemSpacingY = style.ItemSpacing.y;
        style.ItemSpacing.y = 0.f;
        assert(ImGui::GetCurrentWindow() == context.bodyWindow);
        ImGui::PopItemWidth();
        ImGui::Unindent(context.indentSpacing);
        ImGui::EndChild();
        style.ItemSpacing.y = itemSpacingY;

        if (context.storage != nullptr)
        {
            UiExpandedMeasurementState measurement = {
                context.storage->GetFloat(
                    context.measuredHeightKey, 0.f),
                context.storage->GetBool(
                    context.measurementValidKey, false)
            };
            measurement = SubmitUiExpandedMeasurement(
                measurement,
                measuredHeight,
                context.targetOpen,
                context.bodyVisible);
            context.storage->SetFloat(
                context.measuredHeightKey,
                measurement.height);
            context.storage->SetBool(
                context.measurementValidKey,
                measurement.valid);
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
    }

auto UIRenderer::ResolveDisabledPresentationAmount(
        UiDisabledPresentationState& state,
        bool disabled) -> float {
        const int frame = ImGui::GetFrameCount();
        const bool submissionWasInterrupted =
            state.lastSeenFrame >= 0 &&
            state.lastSeenFrame < frame - 1;
        const bool motionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        if (!state.initialized || submissionWasInterrupted)
        {
            state.linearAmount = disabled ? 1.f : 0.f;
            state.initialized = true;
            state.advancedFrame = frame;
        }
        else if (state.advancedFrame != frame)
        {
            state.linearAmount = AdvanceUiDisabledPresentation(
                state.linearAmount,
                disabled,
                ImGui::GetIO().DeltaTime,
                motionEnabled);
            state.advancedFrame = frame;
        }
        state.lastSeenFrame = frame;
        return SmoothUiDisabledPresentation(state.linearAmount);
    }

auto UIRenderer::BeginVisuallyDisabledUiScope(
        const char* id,
        bool disabled) -> bool {
        UiDisabledPresentationState& state =
            g_UiDisabledPresentationStates[ImGui::GetID(id)];
        const float presentationAmount =
            ResolveDisabledPresentationAmount(state, disabled);
        const bool applyManualAlpha =
            g_UiVisualDisabledScopeDepth == 0;
        if (applyManualAlpha)
        {
            ImGui::PushStyleVar(
                ImGuiStyleVar_Alpha,
                ImGui::GetStyle().Alpha *
                    (1.f +
                        (g_UiVisualTokens.controlDisabledAlpha - 1.f) *
                        presentationAmount));
        }
        ImGui::PushStyleVar(
            ImGuiStyleVar_DisabledAlpha,
            1.f);
        ImGui::PushUvsrDisabledPresentation(
            presentationAmount);
        ImGui::BeginDisabled(disabled);
        ++g_UiVisualDisabledScopeDepth;
        return applyManualAlpha;
    }

auto UIRenderer::EndVisuallyDisabledUiScope(bool manualAlphaApplied) -> void {
        assert(g_UiVisualDisabledScopeDepth > 0);
        --g_UiVisualDisabledScopeDepth;
        ImGui::EndDisabled();
        ImGui::PopUvsrDisabledPresentation();
        ImGui::PopStyleVar();
        if (manualAlphaApplied)
            ImGui::PopStyleVar();
    }

auto UIRenderer::BeginAnimatedToggleRegion(
        const char* id,
        bool visible,
        UiToggleRegionOwner owner ,
        UiToggleRegionVisualMode visualMode ) -> bool {
        // BeginChild starts a fresh item-width stack. Carry the enclosing
        // drawer width into toggle regions instead of letting ImGui choose its
        // wider default slider width.
        const float inheritedItemWidth = ImGui::CalcItemWidth();
        const ImGuiID regionId = ImGui::GetID(id);
        UiToggleRegionAnimationState& state =
            g_UiToggleRegionAnimationStates[regionId];
        const int frame = ImGui::GetFrameCount();
        const bool submissionWasInterrupted =
            state.lastSeenFrame >= 0 &&
            state.lastSeenFrame < frame - 2;
        bool targetChangedThisFrame = false;

        if (!state.initialized || submissionWasInterrupted)
        {
            state.linearAmount = visible ? 1.f : 0.f;
            state.disabledPresentationLinearAmount =
                visible ? 0.f : 1.f;
            state.targetVisible = visible;
            state.initialized = true;
            state.transitionFrame = frame;
            state.disabledPresentationAdvancedFrame = frame;
        }
        else if (state.targetVisible != visible)
        {
            // UpdateUI runs after the scene submission. Hold the old endpoint
            // for the frame in which the toggle changed; animation begins on
            // the next UI frame, after the renderer has consumed the setting.
            state.targetVisible = visible;
            state.transitionFrame = frame;
            targetChangedThisFrame = true;
        }

        const bool motionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        if (!motionEnabled)
        {
            state.targetVisible = visible;
            state.linearAmount = visible ? 1.f : 0.f;
            state.disabledPresentationLinearAmount =
                visible ? 0.f : 1.f;
            state.transitionFrame = frame;
            state.advancedFrame = frame;
            state.disabledPresentationAdvancedFrame = frame;
            targetChangedThisFrame = false;
        }
        else if (state.disabledPresentationAdvancedFrame != frame)
        {
            state.disabledPresentationLinearAmount =
                AdvanceUiDisabledPresentation(
                    state.disabledPresentationLinearAmount,
                    !state.targetVisible,
                    ImGui::GetIO().DeltaTime,
                    true);
            state.disabledPresentationAdvancedFrame = frame;
        }

        const bool needsInitialMeasurement =
            motionEnabled &&
            NeedsInitialUiExpandedMeasurement(
                state.targetVisible,
                state.measurement);
        if (needsInitialMeasurement)
        {
            // Keep this first layout pass invisible and at zero progress. The
            // following frame starts from the complete measured height.
            state.linearAmount = 0.f;
            state.transitionFrame = frame;
        }
        else if (frame > state.transitionFrame &&
            state.advancedFrame != frame)
        {
            state.linearAmount = AdvanceUiLayoutAnimation(
                state.linearAmount,
                state.targetVisible);
            state.advancedFrame = frame;
        }

        state.lastSeenFrame = frame;
        const bool transitionActive =
            targetChangedThisFrame ||
            needsInitialMeasurement ||
            (state.linearAmount > 0.f &&
                state.linearAmount < 1.f);
        if (owner == UiToggleRegionOwner::Settings)
        {
            if (transitionActive)
                MarkSettingsLayoutAnimationActive();
            TrackSettingsScrollAnchor(
                regionId,
                ImGui::GetCursorScreenPos().y);
        }
        else if (transitionActive)
        {
            g_PerformanceTableTransitionActive = true;
        }
        if (!state.targetVisible && state.linearAmount <= 0.f)
            return false;

        const float easedAmount =
            SmoothUiLayoutAnimation(state.linearAmount);
        const float layoutAlpha =
            visualMode == UiToggleRegionVisualMode::ClipDuringCollapse &&
                !state.targetVisible
                ? 1.f
                : easedAmount;
        const float disabledPresentationAmount =
            SmoothUiDisabledPresentation(
                state.disabledPresentationLinearAmount);
        const float animatedHeight =
            !motionEnabled
                ? 0.f
                : needsInitialMeasurement
                ? 0.001f
                : std::max(
                    state.measurement.height * easedAmount,
                    0.001f);

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            0.f);
        const bool applyManualDisabledAlpha =
            g_UiVisualDisabledScopeDepth == 0;
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha *
                (needsInitialMeasurement
                    ? 0.f
                    : layoutAlpha) *
                (applyManualDisabledAlpha
                    ? 1.f +
                        (g_UiVisualTokens.controlDisabledAlpha - 1.f) *
                        disabledPresentationAmount
                    : 1.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_DisabledAlpha,
            1.f);
        ImGui::PushUvsrDisabledPresentation(
            disabledPresentationAmount);
        ImGuiWindowFlags childWindowFlags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (motionEnabled &&
            (needsInitialMeasurement ||
            !state.targetVisible ||
            state.linearAmount < 1.f))
        {
            childWindowFlags |=
                ImGuiWindowFlags_NoNavInputs |
                ImGuiWindowFlags_NoNavFocus;
        }
        ImGuiChildFlags childFlags =
            ImGuiChildFlags_AllowZeroSize;
        if (!motionEnabled)
        {
            childFlags |=
                ImGuiChildFlags_AutoResizeY |
                ImGuiChildFlags_AlwaysAutoResize;
        }
        bool bodyVisible = ImGui::BeginChild(
            regionId ^ ImGuiID(0x6C3E91B7u),
            ImVec2(0.f, animatedHeight),
            childFlags,
            childWindowFlags);
        if (owner == UiToggleRegionOwner::Settings)
            TrackSettingsAppearanceDrawList(ImGui::GetWindowDrawList());
        else
            TrackPerformanceAppearanceDrawList(ImGui::GetWindowDrawList());
        EnsureAnimatedChildLayoutSubmission(bodyVisible);
        ImGui::PushItemWidth(inheritedItemWidth);

        // Interaction is blocked during both directions. Opening controls stay
        // visually steady until they become interactive; closing or otherwise
        // gated controls ease through the independent disabled presentation.
        ImGui::BeginDisabled(
            !state.targetVisible || state.linearAmount < 1.f);
        ++g_UiVisualDisabledScopeDepth;
        g_UiToggleRegionAnimationContexts.push_back({
            regionId,
            bodyVisible,
            true
        });
        return true;
    }

auto UIRenderer::EndAnimatedToggleRegion() -> void {
        assert(!g_UiToggleRegionAnimationContexts.empty());
        const UiToggleRegionAnimationContext context =
            g_UiToggleRegionAnimationContexts.back();
        g_UiToggleRegionAnimationContexts.pop_back();
        const float measuredHeight =
            std::max(0.f, ImGui::GetCursorPosY());

        if (context.ownsDisabledPresentationScope)
        {
            assert(g_UiVisualDisabledScopeDepth > 0);
            --g_UiVisualDisabledScopeDepth;
        }
        ImGui::EndDisabled();
        ImGui::PopUvsrDisabledPresentation();
        ImGui::PopItemWidth();
        ImGui::EndChild();

        const auto stateIterator =
            g_UiToggleRegionAnimationStates.find(context.id);
        if (stateIterator != g_UiToggleRegionAnimationStates.end() &&
            context.bodyVisible)
        {
            UiToggleRegionAnimationState& state =
                stateIterator->second;
            state.measurement = SubmitUiExpandedMeasurement(
                state.measurement,
                measuredHeight,
                state.targetVisible,
                context.bodyVisible);
            // A legitimate empty child measures zero. Keep measurement state
            // separate from the numeric result so empty method-specific
            // layouts complete instead of re-entering the hidden measurement
            // pass forever and blocking deferred dropdown commits.
        }

        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor();
    }

auto UIRenderer::BeginMaterialEditorConditionalRegion(
        const char* id,
        bool visible) -> bool {
        return BeginAnimatedToggleRegion(
            id,
            visible,
            UiToggleRegionOwner::Settings);
    }

auto UIRenderer::EndMaterialEditorConditionalRegion() -> void {
        EndAnimatedToggleRegion();
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

auto UIRenderer::DrawUvsrColorEdit(
        const char* label,
        float* color,
        UvsrColorEditChannels channels) -> bool {
        ImGuiColorEditFlags flags =
            ImGuiColorEditFlags_Float |
            ImGuiColorEditFlags_DisplayRGB |
            ImGuiColorEditFlags_NoTooltip;
        if (channels == UvsrColorEditChannels::Rgba)
        {
            flags |=
                ImGuiColorEditFlags_AlphaBar |
                ImGuiColorEditFlags_AlphaPreviewHalf;
        }
        if (!ImGui::IsUvsrStockWidgetRenderingEnabled())
            flags |= ImGuiColorEditFlags_PickerHueWheel;
        return channels == UvsrColorEditChannels::Rgba
            ? ImGui::ColorEdit4(label, color, flags)
            : ImGui::ColorEdit3(label, color, flags);
    }

auto UIRenderer::DrawMaterialEditorColorEdit3(
        const char* label,
        float* color) -> bool {
        return DrawUvsrColorEdit(
            label,
            color,
            UvsrColorEditChannels::Rgb);
    }

auto UIRenderer::GetUiHighlightFade(
        ImGuiID id,
        bool highlighted,
        float speed ) -> float {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID amountKey = id ^ ImGuiID(0xA53C9E21u);
        const ImGuiID frameKey = id ^ ImGuiID(0x6D27F4B3u);
        const float target = highlighted ? 1.f : 0.f;
        if (!ImGui::IsUvsrUiMotionEnabled())
        {
            storage->SetFloat(amountKey, target);
            storage->SetInt(frameKey, ImGui::GetFrameCount());
            return target;
        }

        float amount = storage->GetFloat(amountKey, 0.f);
        const int frame = ImGui::GetFrameCount();
        const int lastFrame = storage->GetInt(frameKey, -2);
        if (lastFrame < frame - 1)
            amount = 0.f;
        const float blend = std::clamp(
            ImGui::GetIO().DeltaTime * speed,
            0.f,
            1.f);
        amount += (target - amount) * blend;
        if (std::abs(target - amount) < 0.015f)
            amount = target;
        storage->SetFloat(amountKey, amount);
        storage->SetInt(frameKey, frame);
        return amount;
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

auto UIRenderer::DrawPresetResetIconAtPlacement(
        const char* id,
        bool modified,
        const char* tooltip,
        SettingsResetIconPlacement placement) -> bool {
        ImGui::PushID(id);
        const ImGuiID resetId = ImGui::GetID("##PresetReset");
        const float visibility =
            GetUiHighlightFade(resetId, modified, 18.f);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonSize = ImGui::GetFrameHeight() * 0.78f;

        const bool nestedDropdownGutterRequested =
            placement == SettingsResetIconPlacement::NestedDropdownGutter;
        const bool nestedDropdownGutterAvailable =
            nestedDropdownGutterRequested &&
            !g_NestedDrawerAnimationContexts.empty() &&
            ImGui::GetCurrentWindow() ==
                g_NestedDrawerAnimationContexts.back().bodyWindow;
        if (nestedDropdownGutterRequested)
        {
            assert(ShouldPlaceUiResetInNestedDropdownGutter(
                true,
                g_NestedDrawerAnimationContexts.size()));
            assert(nestedDropdownGutterAvailable);
        }
        if (nestedDropdownGutterAvailable)
        {
            const NestedDrawerAnimationContext& context =
                g_NestedDrawerAnimationContexts.back();
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            const float resetButtonScreenX =
                ImGui::GetCursorScreenPos().x +
                ResolveNestedDropdownResetOffset(
                    context.indentSpacing,
                    buttonSize);
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
            style.Alpha * visibility);
        ImGui::BeginDisabled(!modified || visibility < 0.98f);
        // Route through the native button frame so the reset control receives
        // the same Amp gradient outline and interaction surface as every other
        // framed menu element.
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

auto UIRenderer::DrawPresetResetIcon(
        const char* id,
        bool modified,
        const char* tooltip ) -> bool {
        return DrawPresetResetIconAtPlacement(
            id,
            modified,
            tooltip,
            SettingsResetIconPlacement::Trailing);
    }

auto UIRenderer::DrawNestedDropdownResetIcon(
        const char* id,
        bool modified,
        const char* tooltip ) -> bool {
        return DrawPresetResetIconAtPlacement(
            id,
            modified,
            tooltip,
            SettingsResetIconPlacement::NestedDropdownGutter);
    }

auto UIRenderer::HasDeferredDropdownUiActions() -> bool {
        return !g_DeferredDropdownUiState.actions.Empty();
    }

auto UIRenderer::CancelDeferredDropdownUiActions() -> void {
        ImGui::FinishComboPopupTransition(
            g_DeferredDropdownUiState.transitionComboId);
        g_DeferredDropdownUiState = {};
    }

auto UIRenderer::IsDeferredDropdownPopupTransitionActive() -> bool {
        return ImGui::IsComboPopupTransitionActive(
            g_DeferredDropdownUiState.transitionComboId);
    }

auto UIRenderer::FinishUnsubmittedDeferredDropdownPopupTransition() -> void {
        const DeferredDropdownUiState& state =
            g_DeferredDropdownUiState;
        if (state.actions.Empty() ||
            state.transitionComboId == 0 ||
            state.transitionComboLastSubmittedFrame ==
                ImGui::GetFrameCount())
        {
            return;
        }

        // A clipped row or collapsed drawer cannot submit the popup frame
        // which advances its retained roll-up. Close only that originating
        // combo so its deferred action cannot remain stranded indefinitely.
        ImGui::FinishComboPopupTransition(state.transitionComboId);
    }

auto UIRenderer::DrawTranslucentHeaderPanelBodySurface(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        float rounding) -> void {
        if (!drawList ||
            bodyRect.GetWidth() <= 1.f ||
            bodyRect.GetHeight() <= 1.f)
        {
            return;
        }

        std::vector<ImRect> exclusions =
            g_SettingsScrollStabilityContext
                .translucentHeaderSupportRects;
        std::sort(
            exclusions.begin(),
            exclusions.end(),
            [](const ImRect& left, const ImRect& right)
            {
                return left.Min.y < right.Min.y;
            });

        const ImU32 bodyColor = ImGui::GetColorU32(
            g_UiVisualTokens.panelBodySurface);
        const auto drawClippedSurface =
            [&](const ImRect& clipRect)
            {
                if (clipRect.GetWidth() <= 0.f ||
                    clipRect.GetHeight() <= 0.f)
                {
                    return;
                }
                drawList->PushClipRect(
                    clipRect.Min,
                    clipRect.Max,
                    true);
                drawList->AddRectFilled(
                    bodyRect.Min,
                    bodyRect.Max,
                    bodyColor,
                    rounding,
                    ImDrawFlags_RoundCornersAll);
                drawList->PopClipRect();
            };

        const float headerSupportInset = std::max(
            1.f,
            ImGui::GetStyle().FrameRounding);
        float nextFullWidthY = bodyRect.Min.y;
        for (ImRect exclusion : exclusions)
        {
            exclusion.Min.x += headerSupportInset;
            exclusion.Min.y += headerSupportInset;
            exclusion.Max.x -= headerSupportInset;
            exclusion.Max.y -= headerSupportInset;
            exclusion.ClipWith(bodyRect);
            if (exclusion.GetWidth() <= 0.f ||
                exclusion.GetHeight() <= 0.f ||
                exclusion.Min.y < nextFullWidthY)
            {
                continue;
            }

            drawClippedSurface(ImRect(
                ImVec2(bodyRect.Min.x, nextFullWidthY),
                ImVec2(bodyRect.Max.x, exclusion.Min.y)));
            drawClippedSurface(ImRect(
                ImVec2(bodyRect.Min.x, exclusion.Min.y),
                ImVec2(exclusion.Min.x, exclusion.Max.y)));
            drawClippedSurface(ImRect(
                ImVec2(exclusion.Max.x, exclusion.Min.y),
                ImVec2(bodyRect.Max.x, exclusion.Max.y)));
            nextFullWidthY = exclusion.Max.y;
        }
        drawClippedSurface(ImRect(
            ImVec2(bodyRect.Min.x, nextFullWidthY),
            bodyRect.Max));

        ImGui::RenderFrameBorder(
            bodyRect.Min,
            bodyRect.Max,
            rounding,
            false);
    }

auto UIRenderer::GetDeferredDropdownPreview(ImGuiID comboId) -> const char* {
        const DeferredDropdownUiPayload* action =
            g_DeferredDropdownUiState.actions.Find(comboId);
        return action && !action->previewValue.empty()
            ? action->previewValue.c_str()
            : nullptr;
    }

auto UIRenderer::QueueDeferredUiAction(
        ImGuiID controlId,
        ImGuiID transitionComboId,
        const char* previewValue,
        std::function<void()> action) -> void {
        assert(controlId != 0);
        DeferredDropdownUiState& state =
            g_DeferredDropdownUiState;
        state.actions.Upsert(
            controlId,
            DeferredDropdownUiPayload{
                previewValue ? previewValue : "",
                std::move(action)
            });
        state.transitionComboId = transitionComboId;
        state.transitionComboLastSubmittedFrame =
            transitionComboId != 0
                ? ImGui::GetFrameCount()
                : -1;
        state.lastRequestTime = ImGui::GetTime();
        state.requestFrame = ImGui::GetFrameCount();
        state.idleStartFrame = -1;
    }

auto UIRenderer::QueueDeferredControlUiAction(
        std::function<void()> action) -> void {
        QueueDeferredUiAction(
            ImGui::GetItemID(),
            0,
            nullptr,
            std::move(action));
    }

auto UIRenderer::QueueDeferredDropdownUiAction(
        const char* previewValue,
        std::function<void()> action) -> void {
        QueueDeferredUiAction(
            g_ActiveRoundedComboId,
            g_ActiveRoundedComboId,
            previewValue,
            std::move(action));
    }

auto UIRenderer::TryApplyDeferredDropdownUiActions(
        bool compositionIdle) -> bool {
        DeferredDropdownUiState& state =
            g_DeferredDropdownUiState;
        if (state.actions.Empty())
            return false;

        const int frame = ImGui::GetFrameCount();
        state.idleStartFrame = UpdateUiDropdownIdleStartFrame(
            state.idleStartFrame,
            frame,
            compositionIdle);
        if (!ShouldCommitDeferredDropdownActions(
                frame,
                state.requestFrame,
                state.idleStartFrame,
                ImGui::GetTime() - state.lastRequestTime))
        {
            return false;
        }
        DeferredUiActionQueue<ImGuiID, DeferredDropdownUiPayload> actions =
            std::move(state.actions);
        state = {};
        return actions.Drain(
            [](ImGuiID, DeferredDropdownUiPayload action)
            {
                if (action.apply)
                    action.apply();
            });
    }

auto UIRenderer::BeginRoundedCombo(
        const char* label,
        const char* previewValue,
        ImGuiComboFlags flags ) -> bool {
        const ImGuiID comboId = ImGui::GetID(label);
        const char* deferredPreview =
            GetDeferredDropdownPreview(comboId);
        const char* visiblePreview =
            deferredPreview ? deferredPreview : previewValue;
        const bool open =
            ImGui::BeginCombo(label, visiblePreview, flags);
        DeferredDropdownUiState& deferredState =
            g_DeferredDropdownUiState;
        if (open && deferredState.transitionComboId == comboId)
        {
            deferredState.transitionComboLastSubmittedFrame =
                ImGui::GetFrameCount();
        }
        g_ActiveRoundedComboId = open ? comboId : 0;
        return open;
    }

auto UIRenderer::DrawPerformancePanelContents(
        float settingsControlWidth,
        const std::string& performanceLine) -> void {
        ImGui::PushItemWidth(settingsControlWidth);

            const float summaryCursorX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(
                summaryCursorX + g_UiSpacingTokens.tight);

            const char* performanceTooltip =
                "tris counts main-pass triangles after frustum culling; "
                "occluded, back-facing, or alpha-discarded ones may remain.";
            ImGui::TextUnformatted(performanceLine.c_str());
            ImGui::SetItemTooltip("%s", performanceTooltip);
            ImGui::SetCursorPosX(summaryCursorX);

            static constexpr const char* StatisticsEffectLabels[] = {
                "Complete Renderer",
                "Scene Setup",
                "Geometry",
                "Path Transport",
                "Direct Lighting",
                "Screen Space Visibility",
                "Directional Shadows",
                "Temporal Reconstructive",
                "Fast Approximate",
                "Multisample Adaptive",
                "Material Picking",
                "Environment Background",
                "Tone Mapping",
                "Output Blit"
            };
            static_assert(
                std::size(StatisticsEffectLabels) ==
                static_cast<size_t>(StatisticsEffect::Count));
            m_StatisticsEffect = std::clamp(
                m_StatisticsEffect,
                0,
                static_cast<int>(StatisticsEffect::Count) - 1);
            if (BeginRoundedCombo(
                    "##StatisticsEffect",
                    StatisticsEffectLabels[m_StatisticsEffect]))
            {
                for (int index = 0;
                    index < static_cast<int>(
                        std::size(StatisticsEffectLabels));
                    ++index)
                {
                    DrawDeferredDropdownOption(
                        StatisticsEffectLabels[index],
                        StatisticsEffectLabels[index],
                        m_StatisticsEffect == index,
                        [selected = &m_StatisticsEffect, index]()
                        {
                            *selected = index;
                        });
                    if (m_StatisticsEffect == index)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the renderer timings shown below.");
            const StatisticsEffect selectedEffect =
                static_cast<StatisticsEffect>(m_StatisticsEffect);

            const RendererTimings& timings =
                m_app->GetRendererTimings();
            uint32_t performanceTimingViewId = 0u;
            static constexpr ImGuiTableFlags StatisticsTableFlags =
                ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp;
            const auto beginStatisticsTable =
                [](const char* identifier, const char* firstColumn)
            {
                if (!ImGui::BeginTable(
                        identifier,
                        2,
                        StatisticsTableFlags))
                    return false;
                ImGui::TableSetupColumn(
                    firstColumn,
                    ImGuiTableColumnFlags_WidthStretch,
                    3.f);
                ImGui::TableSetupColumn(
                    "Current",
                    ImGuiTableColumnFlags_WidthStretch,
                    1.35f);
                return true;
            };
            const auto beginStatisticsRow =
                [](const char* label, bool available)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (available)
                    ImGui::TextUnformatted(label);
                else
                    ImGui::TextDisabled("%s", label);
                ImGui::TableSetColumnIndex(1);
            };
            const auto drawMilliseconds =
                [this, &beginStatisticsRow, &performanceTimingViewId](
                    const char* label, double value, bool available)
            {
                const uvsr::PerformanceTimingRowState rowState =
                    m_PerformanceTimingRows.Resolve(
                        performanceTimingViewId,
                        ImHashStr(label),
                        value,
                        available);
                if (!rowState.IsVisible())
                    return;
                beginStatisticsRow(label, rowState.HasMeasurement());
                if (rowState.HasMeasurement())
                    ImGui::Text("%.3f ms", rowState.milliseconds);
                else
                    ImGui::TextDisabled("--");
            };
            const auto drawCount =
                [&beginStatisticsRow](
                    const char* label, uint64_t value, bool available)
            {
                beginStatisticsRow(label, available);
                if (available)
                    ImGui::Text("%llu", static_cast<unsigned long long>(value));
                else
                    ImGui::TextDisabled("--");
            };
            const auto drawMemory =
                [&beginStatisticsRow](
                    const char* label, uint64_t bytes, bool available)
            {
                beginStatisticsRow(label, available);
                if (available)
                {
                    constexpr double BytesPerMebibyte = 1024.0 * 1024.0;
                    ImGui::Text(
                        "%.2f MiB",
                        double(bytes) / BytesPerMebibyte);
                }
                else
                    ImGui::TextDisabled("--");
            };
            const auto drawText =
                [&beginStatisticsRow](
                    const char* label, const char* value, bool available)
            {
                beginStatisticsRow(label, available);
                if (available)
                    ImGui::TextUnformatted(value);
                else
                    ImGui::TextDisabled("--");
            };
            const auto drawRendererTiming =
                [this, &timings, &drawMilliseconds](
                    const char* label,
                    RendererTimingStage stage,
                    bool eligible = true)
            {
                const bool available =
                    eligible &&
                    m_app->IsRendererStageActiveThisFrame(stage) &&
                    timings.IsAvailable(stage);
                const double measuredValue =
                    available ? timings.Get(stage) : 0.0;
                drawMilliseconds(label, measuredValue, available);
            };
            const auto drawScreenSpaceVisibilityTiming =
                [this, &drawMilliseconds](const char* label)
            {
                const bool available =
                    m_HasVisibilityStatSnapshot &&
                    m_DisplayedVisibilityTimings.active &&
                    m_DisplayedVisibilityTimings.available;
                drawMilliseconds(
                    label,
                    m_DisplayedVisibilityTimings.CompleteEffectMs(),
                    available);
            };
            const auto drawSelectedRendererTable =
                [&beginStatisticsTable, &drawRendererTiming](
                    const char* label, RendererTimingStage stage)
            {
                if (!beginStatisticsTable(
                        "##SelectedRendererStatistics", "Graphics Stage"))
                    return;
                drawRendererTiming(label, stage);
                drawRendererTiming(
                    "Complete Renderer Frame",
                    RendererTimingStage::CompleteFrame);
                ImGui::EndTable();
            };

            const auto drawStatisticsTable =
                [&](StatisticsEffect effect)
            {
            performanceTimingViewId = static_cast<uint32_t>(effect);
            switch (effect)
            {
            case StatisticsEffect::CompleteRenderer:
                if (beginStatisticsTable(
                        "##CompleteRendererStatistics", "Graphics Stage"))
                {
                    static constexpr std::pair<
                        const char*, RendererTimingStage> CompleteRows[] = {
                        { "Complete Renderer Frame",
                            RendererTimingStage::CompleteFrame },
                        { "Scene Setup and Clears",
                            RendererTimingStage::SceneSetup },
                        { "Geometry", RendererTimingStage::Geometry },
                        { "Path Transport",
                            RendererTimingStage::PathTransport },
                        { "Closest Surface Resolve",
                            RendererTimingStage::MultisampleResolve },
                        { "Shadow Ray Dispatch",
                            RendererTimingStage::ShadowRayDispatch },
                        { "Shadow Denoise",
                            RendererTimingStage::ShadowDenoise },
                        { "Sky Visibility Ray Dispatch",
                            RendererTimingStage::SkyVisibilityRayDispatch },
                        { "Sky Visibility Denoise",
                            RendererTimingStage::SkyVisibilityDenoise },
                        { "Direct Lighting",
                            RendererTimingStage::DirectLighting },
                        { "Visibility Lighting Preparation",
                            RendererTimingStage::VisibilityLightingPreparation },
                        { "Screen Space Visibility",
                            RendererTimingStage::ScreenSpaceVisibility },
                        { "Ambient Occlusion Denoise",
                            RendererTimingStage::AmbientOcclusionDenoise },
                        { "Diffuse Illumination Denoise",
                            RendererTimingStage::DiffuseIlluminationDenoise },
                        { "Material Picking",
                            RendererTimingStage::MaterialPicking },
                        { "Environment Background",
                            RendererTimingStage::EnvironmentBackground },
                        { "Auto Exposure",
                            RendererTimingStage::AutoExposure },
                        { "Tone Mapping", RendererTimingStage::ToneMapping },
                        { "Fast Approximate",
                            RendererTimingStage::FastApproximate },
                        { "Output Blit", RendererTimingStage::OutputBlit }
                    };
                    static_assert(
                        std::size(CompleteRows) ==
                        static_cast<size_t>(RendererTimingStage::Count));
                    for (const auto& [label, stage] : CompleteRows)
                    {
                        if (stage ==
                            RendererTimingStage::ScreenSpaceVisibility)
                        {
                            drawScreenSpaceVisibilityTiming(label);
                        }
                        else
                            drawRendererTiming(label, stage);
                    }
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::SceneSetup:
                drawSelectedRendererTable(
                    "Scene Setup", RendererTimingStage::SceneSetup);
                break;
            case StatisticsEffect::Geometry:
                drawSelectedRendererTable(
                    "Geometry", RendererTimingStage::Geometry);
                break;
            case StatisticsEffect::PathTransport:
                drawSelectedRendererTable(
                    "Path Transport", RendererTimingStage::PathTransport);
                break;
            case StatisticsEffect::DirectLighting:
                if (beginStatisticsTable(
                        "##DirectLightingStatistics", "Lighting Stage"))
                {
                    drawRendererTiming(
                        "Direct Lighting",
                        RendererTimingStage::DirectLighting);
                    drawRendererTiming(
                        "Visibility Lighting Preparation",
                        RendererTimingStage::VisibilityLightingPreparation);
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::Visibility:
                if (beginStatisticsTable(
                        "##VisibilityStatistics", "Visibility Metric"))
                {
                    const ScreenSpaceVisibilityTimings& visibility =
                        m_DisplayedVisibilityTimings;
                    const bool available = m_HasVisibilityStatSnapshot;
                    drawMilliseconds(
                        "Complete Effect",
                        visibility.CompleteEffectMs(),
                        available);
                    drawMilliseconds(
                        "First Trace", visibility.firstTraceMs, available);
                    drawMilliseconds(
                        "Upsample",
                        visibility.reconstructionMs,
                        available);
                    drawMilliseconds(
                        "Composition", visibility.compositionMs, available);
                    drawMemory(
                        "Output Texture Memory",
                        visibility.outputTextureBytes,
                        available);
                    drawMemory(
                        "Working Texture Memory",
                        visibility.workingTextureBytes,
                        available);
                    drawRendererTiming(
                        "Ambient Occlusion Denoise",
                        RendererTimingStage::AmbientOcclusionDenoise);
                    drawRendererTiming(
                        "Diffuse Illumination Denoise",
                        RendererTimingStage::DiffuseIlluminationDenoise);
                    drawCount(
                        "Dispatches",
                        visibility.activeDispatchCount,
                        available);
                    drawCount(
                        "Read Resources",
                        visibility.activeSrvCount,
                        available);
                    drawCount(
                        "Write Resources",
                        visibility.activeUavCount,
                        available);
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::Shadows:
                if (beginStatisticsTable(
                        "##ShadowStatistics", "Shadow Metric"))
                {
                    drawRendererTiming(
                        "Shadow Ray Dispatch",
                        RendererTimingStage::ShadowRayDispatch);
                    drawRendererTiming(
                        "Shadow Denoise",
                        RendererTimingStage::ShadowDenoise);
                    drawRendererTiming(
                        "Sky Visibility Ray Dispatch",
                        RendererTimingStage::SkyVisibilityRayDispatch);
                    drawRendererTiming(
                        "Sky Visibility Denoise",
                        RendererTimingStage::SkyVisibilityDenoise);
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::TemporalReconstructive:
                if (beginStatisticsTable(
                        "##TemporalStatistics", "Temporal Metric"))
                {
                    const TemporalAATimings& temporal =
                        m_DisplayedTemporalAATimings;
                    const bool available =
                        m_ui.AntiAliasing.temporal.enabled &&
                        m_HasTemporalAAStatSnapshot;
                    drawMilliseconds(
                        "Complete Effect",
                        temporal.CompleteEffectMilliseconds(),
                        available);
                    drawMilliseconds(
                        "History Blend",
                        temporal.blendMilliseconds,
                        available);
                    drawMilliseconds(
                        "Output",
                        temporal.outputMilliseconds,
                        available);
                    drawMilliseconds(
                        "Presentation Sharpen",
                        temporal.presentationSharpenMilliseconds,
                        available);
                    drawMemory(
                        "Active History Memory",
                        temporal.activeHistoryTextureBytes,
                        available);
                    drawMemory(
                        "Resident History Memory",
                        temporal.residentHistoryTextureBytes,
                        available);
                    drawMemory(
                        "Full-Quality History Memory",
                        temporal.robustHistoryTextureBytes,
                        available);
                    drawMemory(
                        "Minimum-Cost History Memory",
                        temporal.minimumHistoryTextureBytes,
                        available);
                    drawText(
                        "Effective Cost",
                        GetTemporalAaCostModeLabel(
                            temporal.effectiveCostMode),
                        available);
                    beginStatisticsRow(
                        "Minimum History Formats", available);
                    if (!available)
                        ImGui::TextDisabled("--");
                    else if (!temporal.minimumPathSupported)
                        ImGui::TextDisabled("Unsupported");
                    else
                    {
                        ImGui::Text(
                            "%s + %s",
                            temporal.minimumColorIsR11G11B10
                                ? "R11G11B10" : "RGBA16F",
                            temporal.minimumDepthIsR16
                                ? "R16F" : "R32F");
                    }
                    drawText(
                        "History Status",
                        temporal.historyValid ? "Valid" : "Invalid",
                        available);
                    drawCount(
                        "Accumulated Frames",
                        temporal.accumulationCount,
                        available);
                    drawCount(
                        "History Resets",
                        temporal.historyResetCount,
                        available);
                    drawCount(
                        "Dispatches", temporal.dispatchCount, available);
                    drawCount(
                        "History Color Samples",
                        temporal.historyColorSamples,
                        available);
                    drawCount(
                        "History Depth Gathers",
                        temporal.historyDepthGathers,
                        available);
                    drawCount(
                        "History Depth Samples",
                        temporal.historyDepthSamples,
                        available);
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::FastApproximate:
                drawSelectedRendererTable(
                    "Fast Approximate",
                    RendererTimingStage::FastApproximate);
                break;
            case StatisticsEffect::Multisample:
                if (beginStatisticsTable(
                        "##MultisampleStatistics", "Multisample Metric"))
                {
                    const bool enabled = m_ui.AntiAliasing.msaa.enabled;
                    const uint32_t requestedSamples =
                        m_ui.AntiAliasing.msaa.sampleCount;
                    const uint32_t activeSamples =
                        m_app->GetActiveRasterSampleCount();
                    const bool active = enabled && activeSamples > 1u;
                    drawText(
                        "Status",
                        active ? "Active" :
                            enabled ? "Format Unsupported" : "Disabled",
                        true);
                    drawCount(
                        "Requested Samples", requestedSamples, enabled);
                    drawCount("Active Samples", activeSamples, enabled);
                    drawRendererTiming(
                        "Geometry",
                        RendererTimingStage::Geometry,
                        active);
                    drawRendererTiming(
                        "Direct Lighting",
                        RendererTimingStage::DirectLighting,
                        active);
                    drawRendererTiming(
                        "Visibility Lighting Preparation",
                        RendererTimingStage::VisibilityLightingPreparation,
                        active);
                    drawRendererTiming(
                        "Closest Surface Resolve",
                        RendererTimingStage::MultisampleResolve,
                        active);
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::MaterialPicking:
                drawSelectedRendererTable(
                    "Material Picking",
                    RendererTimingStage::MaterialPicking);
                break;
            case StatisticsEffect::EnvironmentBackground:
                drawSelectedRendererTable(
                    "Environment Background",
                    RendererTimingStage::EnvironmentBackground);
                break;
            case StatisticsEffect::ToneMapping:
                if (beginStatisticsTable(
                        "##ToneMappingStatistics", "Graphics Stage"))
                {
                    drawRendererTiming(
                        "Auto Exposure",
                        RendererTimingStage::AutoExposure);
                    drawRendererTiming(
                        "Tone Mapping",
                        RendererTimingStage::ToneMapping);
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::OutputBlit:
                drawSelectedRendererTable(
                    "Output Blit", RendererTimingStage::OutputBlit);
                break;
            default:
                break;
            }
            };

            ImGui::PushStyleVar(
                ImGuiStyleVar_ItemSpacing,
                ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.f));
            for (int effectIndex = 0;
                effectIndex < static_cast<int>(StatisticsEffect::Count);
                ++effectIndex)
            {
                ImGui::PushID(effectIndex);
                if (BeginAnimatedToggleRegion(
                        "##StatisticsTableRegion",
                        selectedEffect ==
                            static_cast<StatisticsEffect>(effectIndex),
                        UiToggleRegionOwner::Performance))
                {
                    drawStatisticsTable(
                        static_cast<StatisticsEffect>(effectIndex));
                    EndAnimatedToggleRegion();
                }
                ImGui::PopID();
            }
            ImGui::PopStyleVar();

        ImGui::PopItemWidth();
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
        if (BeginRoundedCombo(
                "##LightingSolution",
                GetLightingSolutionLabel(m_ui.Lighting)))
        {
            static constexpr std::array<LightingSolution, 2> Solutions = {
                LightingSolution::RayMarching,
                LightingSolution::PathTracing
            };
            for (const LightingSolution solution : Solutions)
            {
                const bool selected = solution == m_ui.Lighting;
                DrawDeferredDropdownOption(
                    GetLightingSolutionLabel(solution),
                    GetLightingSolutionLabel(solution),
                    selected,
                    [this, solution]()
                    {
                        if (m_ui.Lighting == solution)
                            return;
                        ApplyLightingSolution(solution);
                    });
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip(
            "Choose the scene-lighting pipeline. Ray Marching retains the "
            "existing screen-space and selective ray-traced techniques; Path "
            "Tracing uses the shared complete transport core.");

        if (!m_ui.GpuAdapterChoices.empty())
        {
            const GpuAdapterChoice* activeAdapter =
                GetActiveGpuAdapterChoice();
            const char* activeAdapterName = activeAdapter
                ? activeAdapter->name.c_str()
                : "Unknown adapter";

            ImGui::TextUnformatted("Graphics Adapter");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (BeginRoundedCombo(
                    "##GraphicsAdapter",
                    activeAdapterName))
            {
                for (const GpuAdapterChoice& adapter :
                    m_ui.GpuAdapterChoices)
                {
                    const bool selected =
                        adapter.adapterIndex ==
                        m_ui.ActiveGpuAdapterIndex;
                    DrawDeferredDropdownOption(
                        adapter.name.c_str(),
                        adapter.name.c_str(),
                        selected,
                        [this, adapterIndex = adapter.adapterIndex]()
                        {
                            g_RestartAdapterIndex = adapterIndex;
                            g_RestartRequested = true;
                            glfwSetWindowShouldClose(
                                GetDeviceManager()->GetWindow(),
                                GLFW_TRUE);
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

        ImGui::TextUnformatted("Adaptive Sync");
        if (DrawPresetResetIcon(
                "Adaptive Sync",
                m_ui.AdaptiveSync != GetDefaultAdaptiveSyncMode()))
        {
            QueueDeferredControlUiAction(
                [this]()
                {
                    ApplyAdaptiveSyncMode(
                        GetDefaultAdaptiveSyncMode());
                });
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (BeginRoundedCombo(
                "##AdaptiveSync",
                AdaptiveSyncModeLabel(m_ui.AdaptiveSync).data()))
        {
            for (const AdaptiveSyncMode candidate :
                AdaptiveSyncModeValues)
            {
                const bool available =
                    IsAdaptiveSyncModeAvailableForActiveAdapter(candidate);
                const bool selected = candidate == m_ui.AdaptiveSync;
                if (!available)
                    ImGui::BeginDisabled();
                DrawDeferredDropdownOption(
                    AdaptiveSyncModeLabel(candidate).data(),
                    AdaptiveSyncModeLabel(candidate).data(),
                    selected,
                    [this, candidate]()
                    {
                        ApplyAdaptiveSyncMode(candidate);
                    });
                if (!available)
                {
                    ImGui::SetItemTooltip(
                        GetDeviceManager()->
                            IsPresentAllowTearingSupported()
                            ? "Nvidia Exclusive is available only on NVIDIA "
                                "graphics adapters."
                            : "This mode requires DXGI tearing-present "
                                "support on the active system.");
                    ImGui::EndDisabled();
                }
                else
                {
                    switch (candidate)
                    {
                    case AdaptiveSyncMode::Off:
                        ImGui::SetItemTooltip(
                            "Suppress the DXGI Present allow-tearing flag.");
                        break;
                    case AdaptiveSyncMode::VendorAgnostic:
                        ImGui::SetItemTooltip(
                            "Request the generic Windows variable-refresh "
                            "presentation path on any compatible adapter.");
                        break;
                    case AdaptiveSyncMode::NvidiaExclusive:
                        ImGui::SetItemTooltip(
                            "Request the shared Windows variable-refresh path "
                            "only on NVIDIA; the driver and display decide "
                            "whether it engages.");
                        break;
                    case AdaptiveSyncMode::Count:
                        break;
                    }
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip(
            "Choose UVSR's windowed Present policy. VSync stays off; Windows "
            "controls adaptive refresh, which UVSR cannot verify.");

        ImGui::TextUnformatted("Camera Mode");
        if (DrawPresetResetIcon(
                "Camera Mode",
                m_ui.Camera != CameraMode::ThirdPerson))
        {
            m_app->SetCameraMode(CameraMode::ThirdPerson);
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (BeginRoundedCombo(
                "##Camera",
                GetCameraModeLabel(m_ui.Camera)))
        {
            for (const CameraMode mode : SelectableCameraModes)
            {
                const bool selected = mode == m_ui.Camera;
                DrawDeferredDropdownOption(
                    GetCameraModeLabel(mode),
                    GetCameraModeLabel(mode),
                    selected,
                    [this, mode]()
                    {
                        m_app->SetCameraMode(mode);
                    });
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
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
        if (BeginRoundedCombo(
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
                DrawDeferredDropdownOption(
                    scene.DisplayName.c_str(),
                    scene.DisplayName.c_str(),
                    selected,
                    [this, sceneName = scene.FileName]()
                    {
                        m_app->SetCurrentSceneName(sceneName);
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
            const std::filesystem::path sceneFolder =
                m_app->GetSceneDir();
            ShellExecuteW(
                nullptr,
                L"open",
                sceneFolder.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);
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
                g_UiVisualTokens.errorText);
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
                "Path Tracing",
                "One conventional fixed MIS path tracer."))
        {
            return;
        }

        BeginDrawerBody("##PathingBody", settingsControlWidth);
        const WorldSpaceRepresentationStatus& representation =
            m_app->GetWorldSpaceRepresentationStatus();
        const PathTracingCapabilities& capabilities =
            m_app->GetPathTracingCapabilities();
        const PathTracingSceneDomainStatus sceneDomain =
            m_app->GetPathTracingSceneDomainStatus();
        if (!m_ui.Representation.allowRayTraversal)
        {
            ImGui::TextDisabled(
                "Enable Allow Ray Traversal in Representation.");
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
            ImGui::TextWrapped(
                "Fixed balance-heuristic MIS, %u path per pixel each dispatch, %u bounces, roulette after bounce %u.",
                PathTracingSamplesPerFrame,
                PathTracingBounceCount,
                PathTracingRussianRouletteStart);
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
            ImGuiTreeNodeFlags_None,
            m_MaterialDrawerPresentationForceClosed);
        m_MaterialDrawerPresentationForceClosed = false;
        const bool targetOpen =
            g_DrawerAnimationContext.targetOpen;
        m_MaterialDrawerAppearance =
            g_DrawerAnimationContext.openAmount;
        if (targetOpen != m_ui.ShowMaterialDrawer)
        {
            RequestMaterialDrawerVisible(targetOpen);
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
            const FrontEllipsisText formattedMaterialName =
                FormatFrontEllipsisUtf8(material->name, 25u);
            const std::string materialPrefix =
                "Material " + std::to_string(material->materialID) +
                ":";
            ImGui::BeginGroup();
            ImGui::TextUnformatted(materialPrefix.c_str());
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextColored(
                g_UiVisualTokens.successText,
                "%s",
                formattedMaterialName.display.c_str());
            ImGui::EndGroup();
            if (formattedMaterialName.truncated)
            {
                const FrontEllipsisText tooltip =
                    FormatFrontEllipsisUtf8(material->name, 117u);
                ImGui::SetItemTooltip("%s", tooltip.display.c_str());
            }

            ImGui::PushID(material->materialID);

            static constexpr const char* MaterialDomainLabels[] = {
                "Opaque",
                "Alpha-tested",
                "Alpha-blended",
                "Transmissive",
                "Transmissive alpha-tested",
                "Transmissive alpha-blended"
            };
            static_assert(
                std::size(MaterialDomainLabels) ==
                size_t(MaterialDomain::Count));
            const int materialDomainIndex = std::clamp(
                int(material->domain),
                0,
                int(MaterialDomain::Count) - 1);
            SetNextLabeledControlWidth(
                "Material Domain",
                settingsControlWidth);
            if (BeginRoundedCombo(
                    "Material Domain##MaterialDomain",
                    MaterialDomainLabels[materialDomainIndex]))
            {
                for (int index = 0;
                    index < int(MaterialDomain::Count);
                    ++index)
                {
                    const MaterialDomain candidate =
                        MaterialDomain(index);
                    DrawDeferredDropdownOption(
                        MaterialDomainLabels[index],
                        MaterialDomainLabels[index],
                        material->domain == candidate,
                        [app = m_app, material, candidate]()
                        {
                            material->domain = candidate;
                            app->NotifyMaterialCommandChanged(material);
                        });
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose how the selected surface is rendered.");

            ImGui::PushItemWidth(settingsControlWidth);
            const donut::app::MaterialEditorCallbacks materialCallbacks = {
                &BeginMaterialEditorConditionalRegion,
                &EndMaterialEditorConditionalRegion,
                &DrawMaterialEditorTextureFilename,
                &DrawMaterialEditorColorEdit3
            };
            const bool materialChanged =
                donut::app::MaterialEditor(
                    material.get(),
                    false,
                    false,
                    float4(
                        g_UiVisualTokens.successText.x,
                        g_UiVisualTokens.successText.y,
                        g_UiVisualTokens.successText.z,
                        g_UiVisualTokens.successText.w),
                    &materialCallbacks);
            if (materialChanged)
                m_app->NotifyMaterialCommandChanged(material);
            ImGui::PopItemWidth();
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

auto UIRenderer::DrawInterfaceDrawer(float settingsControlWidth) -> void {
        const bool interfaceOpen = DrawCollapsingHeader(
            "Interface",
            "Choose the interface skin and its live colors.");
        if (!interfaceOpen)
        {
            ImGui::Spacing();
            return;
        }

        BeginDrawerBody("##InterfaceBody", settingsControlWidth);

        bool disableAnimations = !m_ui.AnimationsEnabled;
        if (ImGui::Checkbox(
                "Disable Animations",
                &disableAnimations))
        {
            m_ui.AnimationsEnabled = !disableAnimations;
        }
        ImGui::SetItemTooltip(
            "Enable or disable authored interface motion. Ogg remains "
            "immediate regardless of this setting.");

        ImGui::Checkbox(
            "Override Visual Maxes",
            &m_ui.OverrideVisualMaxes);
        ImGui::SetItemTooltip(
            "Allow numeric entry beyond a slider's visible track, up to "
            "the setting's safe supported limits.");

        SetNextLabeledControlWidth(
            "Interface Skin",
            settingsControlWidth);
        if (BeginRoundedCombo(
                "Interface Skin##UiSkin",
                UiSkinLabel(m_ui.Skin).data()))
        {
            for (const UiSkin candidate : UiSkinValues)
            {
                const bool selected = candidate == m_ui.Skin;
                DrawDeferredDropdownOption(
                    UiSkinLabel(candidate).data(),
                    UiSkinLabel(candidate).data(),
                    selected,
                    [this, candidate]()
                    {
                        m_ui.Skin = candidate;
                    });
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip(
            "Choose the interface appearance. Ogg uses standard controls and "
            "disables interface motion for automated experiments.");
        if (DrawPresetResetIcon(
                "Interface Skin",
                m_ui.Skin != DefaultUiSkin))
        {
            QueueDeferredControlUiAction(
                [this]()
                {
                    m_ui.Skin = DefaultUiSkin;
                });
        }

        SetNextLabeledControlWidth(
            "Font Family",
            settingsControlWidth);
        if (BeginRoundedCombo(
                "Font Family##UiFontFamily",
                UiFontFamilyLabel(m_ui.FontFamily).data()))
        {
            for (const UiFontFamily candidate : UiFontFamilyValues)
            {
                const bool available = IsUiFontFamilyAvailable(candidate);
                const bool selected = candidate == m_ui.FontFamily;
                if (!available)
                    ImGui::BeginDisabled();
                DrawDeferredDropdownOption(
                    UiFontFamilyLabel(candidate).data(),
                    UiFontFamilyLabel(candidate).data(),
                    selected,
                    [this, candidate]()
                    {
                        m_ui.FontFamily = candidate;
                    });
                if (!available)
                {
                    const std::string reason =
                        GetUiFontFamilyUnavailableReason(candidate);
                    ImGui::SetItemTooltip("%s", reason.c_str());
                    ImGui::EndDisabled();
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip(
            "Choose the global interface font independently of the interface "
            "skin. Ogg (ProggyClean) uses ProggyClean for body text; Amp "
            "headings remain Noto Sans Bold for clear emphasis.");
        if (DrawPresetResetIcon(
                "Font Family",
                m_ui.FontFamily != DefaultUiFontFamily))
        {
            QueueDeferredControlUiAction(
                [this]()
                {
                    m_ui.FontFamily = DefaultUiFontFamily;
                });
        }

        const auto drawInterfaceColor =
            [&](const char* label,
                const char* id,
                UiRgbaColor& color,
                const UiRgbaColor& defaultColor,
                const char* tooltip,
                bool enabled = true)
            {
                if (!enabled)
                    ImGui::BeginDisabled();
                float values[4] = {
                    color.red,
                    color.green,
                    color.blue,
                    color.alpha
                };
                const std::string controlLabel =
                    std::string(label) + id;
                SetNextLabeledControlWidth(
                    label,
                    settingsControlWidth);
                if (DrawUvsrColorEdit(
                        controlLabel.c_str(),
                        values,
                        UvsrColorEditChannels::Rgba))
                {
                    color = {
                        values[0],
                        values[1],
                        values[2],
                        values[3]
                    };
                }
                ImGui::SetItemTooltip("%s", tooltip);
                if (DrawPresetResetIcon(
                        label,
                        color != defaultColor))
                {
                    color = defaultColor;
                }
                if (!enabled)
                    ImGui::EndDisabled();
            };

        UiSkinPalette unavailablePalette = DefaultUiAmpPalette;
        UiSkinPalette* palette =
            FindUiSkinPalette(m_ui.Accents, m_ui.Skin);
        const UiSkinPalette* defaultPalette =
            FindDefaultUiSkinPalette(m_ui.Skin);
        UiSkinPalette& editablePalette = palette
            ? *palette
            : unavailablePalette;
        const UiSkinPalette& resetPalette = defaultPalette
            ? *defaultPalette
            : DefaultUiAmpPalette;
        const bool authoredPaletteAvailable =
            palette != nullptr && defaultPalette != nullptr;
        constexpr const char* StockColorTooltip =
            "Ogg preserves stock ImGui colors and has no single authored "
            "value for this role.";

        drawInterfaceColor(
            "Primary Accent",
            "##UiPrimaryAccent",
            editablePalette.primaryAccent,
            resetPalette.primaryAccent,
            authoredPaletteAvailable
                ? "Set drawer headers, panel titles, footer buttons, "
                    "selection details, and slider knobs for the active "
                    "authored skin."
                : StockColorTooltip,
            authoredPaletteAvailable);
        drawInterfaceColor(
            "Secondary Accent",
            "##UiSecondaryAccent",
            m_ui.Accents.secondaryAccent,
            DefaultUiSecondaryAccent,
            "Set the secondary accent used by errors and disabled/off "
            "toggle knobs. The Amp default is translucent blue.");
        drawInterfaceColor(
            "Tertiary Accent",
            "##UiTertiaryAccent",
            m_ui.Accents.tertiaryAccent,
            DefaultUiTertiaryAccent,
            "Set success output, Material status, and enabled/on knobs. Amp "
            "defaults to the compensated blue used for a light track.");

        drawInterfaceColor(
            "Font Color",
            "##UiFontColor",
            editablePalette.fontColor,
            resetPalette.fontColor,
            authoredPaletteAvailable
                ? "Set all authored interface text, including drawer "
                    "and panel titles."
                : StockColorTooltip,
            authoredPaletteAvailable);
        drawInterfaceColor(
            "Background Color",
            "##UiPrimaryBackgroundColor",
            editablePalette.primaryBackground,
            resetPalette.primaryBackground,
            authoredPaletteAvailable
                ? "Set menu body, resting controls, and picker background; "
                    "hover, active, body, and picker opacity derive from it."
                : StockColorTooltip,
            authoredPaletteAvailable);

        EndDrawerBody();
        ImGui::Spacing();
    }

auto UIRenderer::BuildPerformanceLine(
        const std::array<std::string, 4>& values) -> std::string {
        return values[0] + " / " +
            values[3] + " / " +
            values[1] + " / " +
            values[2];
    }

auto UIRenderer::QueueStatSnapshot(int width, int height) -> void {
        constexpr double StatUpdateIntervalSeconds = 1.0 / 24.0;
        const double currentFrameTime = std::max(
            0.0,
            double(ImGui::GetIO().DeltaTime));
        m_StatSnapshotElapsed += currentFrameTime;
        if (currentFrameTime > 0.0)
        {
            m_StatFrameTimeSum += currentFrameTime;
            ++m_StatFrameTimeCount;
        }

        const bool captureInitialSnapshot =
            !m_HasAppliedStatSnapshot &&
            m_StatUpdateQueue.empty();
        if (!captureInitialSnapshot &&
            m_StatSnapshotElapsed < StatUpdateIntervalSeconds)
        {
            return;
        }

        StatSnapshot snapshot;
        snapshot.width = width;
        snapshot.height = height;
        snapshot.submittedTriangles =
            m_app->GetSubmittedMainViewTriangles();
        snapshot.frameTimeSeconds = m_StatFrameTimeCount > 0
            ? m_StatFrameTimeSum / double(m_StatFrameTimeCount)
            : m_DisplayedFrameTime;
        if (const ScreenSpaceVisibilityTimings* timings =
                m_app->GetScreenSpaceVisibilityTimings())
        {
            snapshot.visibilityTimings = *timings;
            snapshot.hasVisibilityTimings =
                timings->active && timings->available;
        }
        if (const TemporalAATimings* timings =
                m_app->GetTemporalAATimings())
        {
            snapshot.temporalAATimings = *timings;
            snapshot.hasTemporalAATimings =
                m_ui.AntiAliasing.temporal.enabled &&
                timings->dispatchCount > 0u &&
                timings->available;
        }
        // Keep a complete snapshot as the queue's atomic update unit. If a
        // future render path ever delays consumption, replace its stale pending
        // sample instead of replaying old statistics.
        if (m_StatUpdateQueue.empty())
            m_StatUpdateQueue.push_back(std::move(snapshot));
        else
            m_StatUpdateQueue.back() = std::move(snapshot);

        m_StatSnapshotElapsed = captureInitialSnapshot
            ? 0.0
            : std::fmod(
                m_StatSnapshotElapsed,
                StatUpdateIntervalSeconds);
        m_StatFrameTimeSum = 0.0;
        m_StatFrameTimeCount = 0;
    }

auto UIRenderer::ApplyQueuedStatSnapshot() -> void {
        if (m_StatUpdateQueue.empty())
            return;

        const StatSnapshot snapshot =
            std::move(m_StatUpdateQueue.front());
        m_StatUpdateQueue.pop_front();
        m_HasAppliedStatSnapshot = true;
        m_DisplayedFrameTime = snapshot.frameTimeSeconds;

        FormatStatLine(
            m_PerformanceStatValues[0],
            "%d x %d",
            snapshot.width,
            snapshot.height);
        m_PerformanceStatValues[3] =
            FormatTriangleCount(snapshot.submittedTriangles);
        if (m_DisplayedFrameTime > 0.0)
        {
            FormatStatLine(
                m_PerformanceStatValues[1],
                "%.1f ms",
                m_DisplayedFrameTime * 1e3);
            FormatStatLine(
                m_PerformanceStatValues[2],
                "%.1f fps",
                1.0 / m_DisplayedFrameTime);
        }
        else
        {
            m_PerformanceStatValues[1].clear();
            m_PerformanceStatValues[2].clear();
        }

        m_DisplayedVisibilityTimings = snapshot.visibilityTimings;
        m_DisplayedTemporalAATimings = snapshot.temporalAATimings;
        m_HasVisibilityStatSnapshot = snapshot.hasVisibilityTimings;
        m_HasTemporalAAStatSnapshot = snapshot.hasTemporalAATimings;
    }

auto UIRenderer::DrawBoundedSliderFloat(
        const char* label,
        float* value,
        float logicalMinimum,
        float logicalMaximum,
        float travelMinimum,
        float travelMaximum,
        const char* format ,
        ImGuiSliderFlags flags ) -> bool {
        assert(logicalMinimum <= travelMinimum);
        assert(travelMinimum <= travelMaximum);
        assert(travelMaximum <= logicalMaximum);
        const ImGuiSliderFlags effectiveFlags =
            flags |
            (m_ui.OverrideVisualMaxes
                ? ImGuiSliderFlags_None
                : ImGuiSliderFlags_AlwaysClamp);
        const bool changed = ImGui::SliderFloat(
            label,
            value,
            travelMinimum,
            travelMaximum,
            format,
            effectiveFlags);
        if (changed)
        {
            *value = std::clamp(
                *value,
                logicalMinimum,
                logicalMaximum);
        }
        return changed;
    }

auto UIRenderer::DrawSliderFloat(
        const char* label,
        float* value,
        float minimum,
        float maximum,
        const char* format ,
        ImGuiSliderFlags flags ) -> bool {
        return DrawBoundedSliderFloat(
            label,
            value,
            minimum,
            maximum,
            minimum,
            maximum,
            format,
            flags);
    }

auto UIRenderer::DrawBoundedSliderInt(
        const char* label,
        int* value,
        int logicalMinimum,
        int logicalMaximum,
        int travelMinimum,
        int travelMaximum,
        const char* format ,
        ImGuiSliderFlags flags ) -> bool {
        assert(logicalMinimum <= travelMinimum);
        assert(travelMinimum <= travelMaximum);
        assert(travelMaximum <= logicalMaximum);
        const ImGuiSliderFlags effectiveFlags =
            flags |
            (m_ui.OverrideVisualMaxes
                ? ImGuiSliderFlags_None
                : ImGuiSliderFlags_AlwaysClamp);
        const bool changed = ImGui::SliderInt(
            label,
            value,
            travelMinimum,
            travelMaximum,
            format,
            effectiveFlags);
        if (changed)
        {
            *value = std::clamp(
                *value,
                logicalMinimum,
                logicalMaximum);
        }
        return changed;
    }

auto UIRenderer::DrawSliderInt(
        const char* label,
        int* value,
        int minimum,
        int maximum,
        const char* format ,
        ImGuiSliderFlags flags ) -> bool {
        return DrawBoundedSliderInt(
            label,
            value,
            minimum,
            maximum,
            minimum,
            maximum,
            format,
            flags);
    }

auto UIRenderer::DrawLightDirectionSliders(
        double3& direction,
        bool directional) -> bool {
        auto [azimuth, elevation] = GetCommandLightAngles(
            direction,
            directional);
        bool changed = DrawSliderFloat(
            "Azimuth",
            &azimuth,
            -180.f,
            180.f,
            "%.1f deg",
            ImGuiSliderFlags_NoRoundToFormat);
        changed |= DrawSliderFloat(
            "Elevation",
            &elevation,
            -90.f,
            90.f,
            "%.1f deg",
            ImGuiSliderFlags_NoRoundToFormat);
        if (changed)
        {
            direction = MakeCommandLightDirection(
                azimuth,
                elevation,
                directional);
        }
        return changed;
    }

auto UIRenderer::DrawCenteredActionButton(const char* label, float width) -> bool {
        const ImGuiStyle& style = ImGui::GetStyle();
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
        g_SettingsAppearanceDrawLists.clear();
        g_PerformanceAppearanceDrawLists.clear();
        g_PerformanceTableTransitionActive = false;
        for (UiBackdropRect& backdropRect : m_ui.BackdropRects)
        {
            backdropRect.visible = false;
            backdropRect.compositeExclusions.clear();
        }

        m_ComposedUiSkin = m_ui.Skin;
        ApplyUiSkin(
            m_ComposedUiSkin,
            m_ui.Accents,
            m_ui.AnimationsEnabled,
            m_UiDisplayScale);
        const bool uiMotionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        int width, height;
        GetDeviceManager()->GetWindowDimensions(width, height);
        ImFont* activeUiFont = GetActiveUiFont();
        m_SettingsPanelMarginPixels = static_cast<uint32_t>(
            std::max(
                1.f,
                std::round(g_UiSpacingTokens.section)));
        const ImGuiViewport* mainViewport =
            ImGui::GetMainViewport();
        const UiCommandLayoutRect workRectangle = {
            mainViewport->WorkPos.x,
            mainViewport->WorkPos.y,
            mainViewport->WorkPos.x + mainViewport->WorkSize.x,
            mainViewport->WorkPos.y + mainViewport->WorkSize.y
        };
        ImGui::PushFont(activeUiFont);
        const float minimumCommandHeight =
            std::max(
                GetCommandInterfaceMinimumHeight(),
                ImGui::GetStyle().WindowMinSize.y);
        const float reservedCommandHeight =
            GetCommandInterfaceReservedHeight();
        const float minimumCommandWidth =
            ImGui::GetStyle().WindowMinSize.x;
        const float panelTitleMinimumHeight =
            GetPanelTitleHeight(
                ImGui::GetStyle(),
                ImGui::GetFontSize());
        const float performanceCollapsedHeight =
            panelTitleMinimumHeight +
            ImGui::GetStyle().WindowPadding.y * 2.f +
            ImGui::GetFontSize() +
            g_UiSpacingTokens.tight;
        const float minimumSettingsHeight =
            std::max(
                GetSettingsMinimumExpandedWindowHeight(
                    ImGui::GetStyle(),
                    ImGui::GetFontSize()),
                ImGui::GetStyle().WindowMinSize.y);
        const float panelSeparation =
            g_UiSpacingTokens.tight;
        const float minimumPanelStackHeight =
            performanceCollapsedHeight +
            panelSeparation +
            minimumSettingsHeight;
        ImGui::PopFont();
        m_CommandLayout = ResolveCommandInterfaceLayout(
            workRectangle,
            float(m_SettingsPanelMarginPixels),
            g_UiSpacingTokens.tight,
            260.f * m_UiDisplayScale,
            minimumCommandWidth,
            reservedCommandHeight,
            minimumCommandHeight,
            minimumPanelStackHeight);
        const float panelStackMaximumBottom =
            m_CommandLayout.fits
                ? ResolveSettingsMaximumBottom(
                    m_CommandLayout,
                    workRectangle,
                    float(m_SettingsPanelMarginPixels),
                    uiMotionEnabled
                        ? SmoothPixelZoomVisibility(m_CommandAppearance)
                        : m_CommandAppearance)
                : workRectangle.maxY -
                    float(m_SettingsPanelMarginPixels);
        const bool sceneLoading = m_app->IsSceneBusy();
        if (sceneLoading)
        {
            if (!m_WasSceneLoading)
            {
                // A load can replace scene-owned objects referenced by queued
                // UI actions. Discard those stale choices before the loading
                // screen starts; a scene choice that initiated this load has
                // already been moved out of the queue and applied.
                CancelDeferredDropdownUiActions();
                m_WasSceneLoading = true;
                m_DisplayedFrameTime = 0.0;
                m_StatSnapshotElapsed = 0.0;
                m_StatFrameTimeSum = 0.0;
                m_StatFrameTimeCount = 0;
                m_StatUpdateQueue.clear();
                for (std::string& value : m_PerformanceStatValues)
                    value.clear();
                m_DisplayedVisibilityTimings = {};
                m_DisplayedTemporalAATimings = {};
                m_HasAppliedStatSnapshot = false;
                m_HasVisibilityStatSnapshot = false;
                m_HasTemporalAAStatSnapshot = false;
                m_SettingsAppearance = 0.f;
                m_MaterialDrawerAppearance = 0.f;
                m_SceneLoadCounterStart =
                    std::chrono::steady_clock::now();
                m_SceneLoadHistoryKey = m_app->GetCurrentSceneName();
                m_SceneLoadFailed = false;
            }

            BeginFullScreenWindow();
            ImGui::PushFont(activeUiFont);
            ApplyActiveUiWordSpacing();

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
            static constexpr const char* LoadingDots[] = {
                ".",
                "..",
                "..."
            };
            const size_t loadingDotIndex =
                uiMotionEnabled
                    ? size_t(ImGui::GetTime() * 2.0) %
                        std::size(LoadingDots)
                    : std::size(LoadingDots) - 1u;

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
            const auto sceneTiming = m_SceneLoadTimingByScene.find(
                m_SceneLoadHistoryKey);
            if (sceneTiming != m_SceneLoadTimingByScene.end())
            {
                averageLoadTicks = ResolveAverageSceneLoadTicks(
                    sceneTiming->second);
            }
            if (averageLoadTicks == 0u)
            {
                averageLoadTicks = ResolveAverageSceneLoadTicks(
                    m_AllSceneLoadTiming);
            }
            const std::string averageLoadLabel = averageLoadTicks > 0u
                ? std::to_string(averageLoadTicks)
                : "--";
            snprintf(
                messageBuffer,
                std::size(messageBuffer),
                "Loading scene: %s, please wait%s\n"
                "%s: %llu/%s\n"
                "Objects: %u/%u / Import steps: %llu/%llu / "
                "Textures decoded: %u/%u / GPU ready: %u/%u",
                sceneDisplayName.c_str(),
                LoadingDots[loadingDotIndex],
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

            RestoreActiveUiWordSpacing();
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
                    m_AllSceneLoadTiming,
                    completedLoadMilliseconds);
                if (!RecordBoundedSceneLoadDuration(
                        m_SceneLoadTimingByScene,
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
        ApplyActiveUiWordSpacing();

        float const fontSize = ImGui::GetFontSize();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float settingsControlWidth =
            ImGui::CalcTextSize(
                "Bitmask Directional Visibility").x +
            style.FramePadding.x * 2.f;
        RefreshSettingsSnapshot();

        QueueStatSnapshot(width, height);
        ApplyQueuedStatSnapshot();
        m_SettingsAppearance = uiMotionEnabled
            ? AdvancePixelZoomVisibility(
                m_SettingsAppearance,
                m_ui.ShowUI,
                ImGui::GetIO().DeltaTime)
            : m_ui.ShowUI ? 1.f : 0.f;
        const auto deferredDropdownCompositionIdle =
            [&](bool settingsLayoutIdle, bool settingsScrollIdle)
            {
                const bool settingsAppearanceIdle =
                    m_SettingsAppearance <= 0.f ||
                    m_SettingsAppearance >= 1.f;
                const bool pixelZoomAppearanceIdle =
                    IsPixelZoomCompositionIdle(
                        m_ui.PixelZoom,
                        m_RenderedPixelZoom,
                        m_PendingPixelZoom,
                        m_PixelZoomVisibility,
                        m_PixelZoomLevelTransition);
                const bool interactionIdle =
                    !ImGui::IsAnyItemActive() &&
                    std::abs(ImGui::GetIO().MouseWheel) <= 0.001f &&
                    !ImGui::IsMouseDragging(
                        ImGuiMouseButton_Left);
                return settingsLayoutIdle &&
                    settingsScrollIdle &&
                    settingsAppearanceIdle &&
                    pixelZoomAppearanceIdle &&
                    !IsDeferredDropdownPopupTransitionActive() &&
                    interactionIdle;
            };
        if (!m_ui.ShowUI && m_SettingsAppearance <= 0.f)
        {
            FinishUnsubmittedDeferredDropdownPopupTransition();
            const SettingsScrollStabilityContext& scrollContext =
                g_SettingsScrollStabilityContext;
            const bool recentLayoutAnimation =
                scrollContext.lastFrame >= ImGui::GetFrameCount() - 1 &&
                scrollContext.layoutAnimatingLastFrame;
            TryApplyDeferredDropdownUiActions(
                deferredDropdownCompositionIdle(
                    !recentLayoutAnimation,
                    true));
            RestoreActiveUiWordSpacing();
            ImGui::PopFont();
            return;
        }
        const float settingsAppearanceOpacity =
            SmoothPixelZoomVisibility(m_SettingsAppearance);
        const float settingsAppearanceScale =
            PixelZoomMinimumWindowScale +
            (1.f - PixelZoomMinimumWindowScale) *
                settingsAppearanceOpacity;
        const std::string performanceLine =
            BuildPerformanceLine(m_PerformanceStatValues);

        // Keep Settings independent of live status digits. At the current
        // 23.44 font heights is exactly 20 percent narrower than the previous
        // panel while retaining the longest intentional control width. Root
        // and drawer padding plus the authored scrollbar form a hard content
        // floor; narrow viewports still cap the panel early enough to leave a
        // functional ColorEdit picker lane on the right.
        constexpr float SettingsWindowWidthInFontHeights = 23.44f;
        const float settingsPanelMarginPixels =
            float(m_SettingsPanelMarginPixels);
        const float availableWindowWidth =
            std::max(
                1.f,
                workRectangle.maxX -
                    workRectangle.minX -
                    settingsPanelMarginPixels * 2.f);
        const float colorPickerMinimumContentWidth =
            ImGui::GetUvsrAuthoredColorPickerMinimumWidth(
                ImGui::GetFrameHeight());
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
        const float settingsWindowWidth = std::min(
            std::max(
                fontSize * SettingsWindowWidthInFontHeights,
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
        ImGui::SetNextWindowPos(
            ImVec2(
                workRectangle.minX + settingsPanelMarginPixels,
                performanceWindowTop),
            ImGuiCond_Always);
        // Exact-X constraints own the fixed panel width while
        // AlwaysAutoResize measures Y. Reapplying a zero Y size every frame
        // would keep AutoFitFramesY armed after the window is logically
        // collapsed, causing Begin() to report visible and submit compact-body
        // children at the settled endpoint.
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(settingsWindowWidth, performanceCollapsedHeight),
            ImVec2(
                settingsWindowWidth,
                performanceMaximumWindowHeight));
        ImGui::SetNextUvsrWindowCollapsedHeight(
            performanceCollapsedHeight);
        if (m_PerformanceCollapsedRequest)
        {
            ImGui::SetNextUvsrWindowCollapseTarget(
                *m_PerformanceCollapsedRequest);
            ImGui::SetNextWindowCollapsed(
                *m_PerformanceCollapsedRequest,
                ImGuiCond_Always);
            m_PerformanceCollapsedRequest.reset();
        }
        else
        {
            ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
        }

        PushPanelBodySurface();
        ImGui::PushStyleColor(
            ImGuiCol_TitleBg,
            g_UiVisualTokens.settingsTitleSurface);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgActive,
            g_UiVisualTokens.settingsTitleSurface);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgCollapsed,
            g_UiVisualTokens.settingsTitleSurface);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            g_UiVisualTokens.settingsTitleText);
        ImGuiWindowFlags performanceWindowFlags =
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings;
        if (!m_ui.ShowUI ||
            m_SettingsAppearance < 1.f)
        {
            performanceWindowFlags |= ImGuiWindowFlags_NoInputs;
        }
        const bool useAuthoredHeaderFont =
            m_ComposedUiSkin != UiSkin::Og;
        if (useAuthoredHeaderFont)
        {
            ImGui::PushFont(GetActiveUiHeaderFont());
            ApplyActiveUiHeaderWordSpacing();
        }
        const bool performanceBeginVisible = ImGui::Begin(
            "Performance",
            nullptr,
            performanceWindowFlags);
        const bool performanceCollapsed = ImGui::IsWindowCollapsed();
        const bool performanceBodySubmitted =
            performanceBeginVisible && !performanceCollapsed;
        if (useAuthoredHeaderFont)
        {
            RestoreActiveUiHeaderWordSpacing();
            ImGui::PopFont();
        }
        ImGui::PopStyleColor();
        ImDrawList* performanceWindowDrawList =
            ImGui::GetWindowDrawList();
        const ImGuiWindow* performanceWindow =
            ImGui::GetCurrentWindow();
        const ImRect performanceBodyRect(
            ImVec2(
                performanceWindow->Pos.x,
                performanceWindow->Pos.y +
                    performanceWindow->TitleBarHeight),
            ImVec2(
                performanceWindow->Pos.x +
                    performanceWindow->Size.x,
                performanceWindow->Pos.y +
                    performanceWindow->Size.y));
        const ImRect performanceContentRect(
            ImVec2(
                performanceBodyRect.Min.x + style.WindowPadding.x,
                performanceBodyRect.Min.y + style.WindowPadding.y),
            ImVec2(
                performanceBodyRect.Max.x - style.WindowPadding.x,
                performanceBodyRect.Max.y - style.WindowPadding.y));
        const ImRect performanceRetainedContentRect(
            performanceContentRect.Min,
            ImVec2(
                performanceContentRect.Max.x,
                std::min(
                    performanceContentRect.Max.y,
                    performanceContentRect.Min.y + fontSize +
                        g_UiSpacingTokens.tight)));
        const float performanceCollapseRange =
            performanceWindow->SizeFull.y -
                performanceCollapsedHeight;
        const bool performanceExpandedRangeKnown =
            performanceCollapseRange > 0.5f;
        const float performanceCollapseAmount =
            performanceExpandedRangeKnown
                ? std::clamp(
                    (performanceWindow->SizeFull.y -
                        performanceWindow->Size.y) /
                        performanceCollapseRange,
                    0.f,
                    1.f)
                : performanceWindow->Size.y <=
                    performanceCollapsedHeight + 0.5f
                    ? 1.f
                    : 0.f;
        // The summary is the Performance counterpart of Settings' retained
        // snapshot row. Start the expanded inner frame below it so the opaque
        // top margin supports the row's rounded fill instead of exposing four
        // separately antialiased scene-backed corners. Morph that boundary to
        // the retained rectangle during collapse so the fillets never flip or
        // snap at the compact endpoint.
        const ImRect performanceExpandedContentRect(
            ImVec2(
                performanceContentRect.Min.x,
                performanceRetainedContentRect.Max.y),
            performanceContentRect.Max);
        const ImRect performanceAnimatedContentRect(
            ImLerp(
                performanceExpandedContentRect.Min,
                performanceRetainedContentRect.Min,
                performanceCollapseAmount),
            ImLerp(
                performanceExpandedContentRect.Max,
                performanceRetainedContentRect.Max,
                performanceCollapseAmount));
        if (performanceBodySubmitted)
        {
            DrawRootPanelBodySurface(
                performanceWindowDrawList,
                performanceBodyRect,
                performanceAnimatedContentRect,
                performanceRetainedContentRect,
                style.WindowRounding);
            DrawPerformancePanelContents(
                settingsControlWidth,
                performanceLine);
            DrawRootPanelBodyOutlines(
                performanceWindowDrawList,
                performanceBodyRect,
                performanceAnimatedContentRect,
                style.WindowRounding);
        }
        else if (performanceCollapsed)
        {
            DrawCompactRootPanelBody(
                performanceWindowDrawList,
                performanceBodyRect,
                performanceRetainedContentRect,
                style.WindowRounding,
                performanceLine.c_str());
        }
        const bool performanceScrollIdle =
            performanceWindow->ScrollTarget.y >= FLT_MAX;
        const ImVec2 performanceWindowPosition =
            ImGui::GetWindowPos();
        const ImVec2 performanceWindowSize =
            ImGui::GetWindowSize();
        const bool performanceCollapseTransitionActive =
            ImGui::IsCurrentUvsrWindowCollapseTransitionActive();
        ImGui::End();
        ImGui::PopStyleColor(4);

        const float settingsWindowTop =
            performanceWindowPosition.y +
            performanceWindowSize.y +
            panelSeparation;
        const float settingsMaximumWindowHeight =
            std::max(
                1.f,
                panelStackMaximumBottom - settingsWindowTop);
        ImGui::SetNextWindowPos(
            ImVec2(
                workRectangle.minX + settingsPanelMarginPixels,
                settingsWindowTop),
            ImGuiCond_Always);
        // Width is fixed by the exact-X constraints below; Y remains owned by
        // AlwaysAutoResize so a settled collapse cannot be kept artificially
        // visible by a perpetually rearmed zero-height auto-fit request.
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(settingsWindowWidth, 0.f),
            ImVec2(
                settingsWindowWidth,
                settingsMaximumWindowHeight));
        const float settingsCollapsedHeight =
            GetSettingsCollapsedWindowHeight(
                style,
                fontSize);
        ImGui::SetNextUvsrWindowCollapsedHeight(
            settingsCollapsedHeight);
        if (m_SettingsCollapsedRequest)
        {
            ImGui::SetNextUvsrWindowCollapseTarget(
                *m_SettingsCollapsedRequest);
            ImGui::SetNextWindowCollapsed(
                *m_SettingsCollapsedRequest,
                ImGuiCond_Always);
            m_SettingsCollapsedRequest.reset();
        }
        else
        {
            ImGui::SetNextWindowCollapsed(false, ImGuiCond_Once);
        }
        // WindowBg is absent beneath title bars. Authored dark skins use the
        // body-composited token; an ultra-bright title intentionally retains
        // the raw translucent header so the backdrop remains visible through it.
        PushPanelBodySurface();
        ImGui::PushStyleColor(
            ImGuiCol_TitleBg,
            g_UiVisualTokens.settingsTitleSurface);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgActive,
            g_UiVisualTokens.settingsTitleSurface);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgCollapsed,
            g_UiVisualTokens.settingsTitleSurface);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            g_UiVisualTokens.settingsTitleText);
        ImGuiWindowFlags settingsWindowFlags =
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (g_UiVisualTokens.sceneTranslucentHeaders)
            settingsWindowFlags |= ImGuiWindowFlags_NoBackground;
        if (!m_ui.ShowUI ||
            m_SettingsAppearance < 1.f)
        {
            settingsWindowFlags |= ImGuiWindowFlags_NoInputs;
        }
        if (useAuthoredHeaderFont)
        {
            ImGui::PushFont(GetActiveUiHeaderFont());
            ApplyActiveUiHeaderWordSpacing();
        }
        const bool settingsBeginVisible = ImGui::Begin(
            "Settings",
            nullptr,
            settingsWindowFlags);
        const bool settingsCollapsed = ImGui::IsWindowCollapsed();
        const bool settingsBodySubmitted =
            settingsBeginVisible && !settingsCollapsed;
        if (useAuthoredHeaderFont)
        {
            RestoreActiveUiHeaderWordSpacing();
            ImGui::PopFont();
        }
        ImGui::PopStyleColor();
        ImDrawList* settingsWindowDrawList =
            ImGui::GetWindowDrawList();
        ImGuiWindow* settingsHeaderWindow =
            ImGui::GetCurrentWindow();
        const ImRect settingsHeaderBodyRect(
            ImVec2(
                settingsHeaderWindow->Pos.x,
                settingsHeaderWindow->Pos.y +
                    settingsHeaderWindow->TitleBarHeight),
            ImVec2(
                settingsHeaderWindow->Pos.x +
                    settingsHeaderWindow->Size.x,
                settingsHeaderWindow->Pos.y +
                    settingsHeaderWindow->Size.y));
        const float settingsCollapseRange =
            settingsHeaderWindow->SizeFull.y -
                settingsCollapsedHeight;
        const bool settingsExpandedRangeKnown =
            settingsCollapseRange > 0.5f;
        const float settingsCollapseAmount =
            settingsExpandedRangeKnown
                ? std::clamp(
                    (settingsHeaderWindow->SizeFull.y -
                        settingsHeaderWindow->Size.y) /
                        settingsCollapseRange,
                    0.f,
                    1.f)
                : settingsHeaderWindow->Size.y <=
                    settingsCollapsedHeight + 0.5f
                    ? 1.f
                    : 0.f;
        const ImRect settingsRetainedContentRect(
            ImVec2(
                settingsHeaderBodyRect.Min.x +
                    style.WindowPadding.x,
                settingsHeaderBodyRect.Min.y +
                    style.WindowPadding.y),
            ImVec2(
                settingsHeaderBodyRect.Max.x -
                    style.WindowPadding.x,
                std::min(
                    settingsHeaderBodyRect.Max.y -
                        style.WindowPadding.y,
                    settingsHeaderBodyRect.Min.y +
                        style.WindowPadding.y + fontSize +
                        g_UiSpacingTokens.tight)));
        bool settingsScrollIdle = true;
        bool settingsLayoutIdle = true;
        g_SettingsScrollStabilityContext
            .translucentHeaderSupportRects.clear();

        ImVec2 expandedSettingsSnapshotMinimum{};
        bool expandedSettingsSnapshotSubmitted = false;
        if (settingsBodySubmitted)
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
        PrepareSettingsScrollStability();
        const float settingsBodyMinimumHeight =
            GetSettingsBodyMinimumHeight(
                settingsBodyMaxHeight);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0.f, settingsBodyMinimumHeight),
            ImVec2(FLT_MAX, settingsBodyMaxHeight));
        // A submitted Settings body always owns its scrollbar channel. Toggling
        // NoScrollbar during root motion changes the child work width and makes
        // every full-width drawer header jump. The fully collapsed frame skips
        // this child entirely, so no scrollbar is emitted at that endpoint.
        const ImGuiWindowFlags settingsBodyFlags =
            ImGuiWindowFlags_AlwaysVerticalScrollbar |
            (settingsCollapseAmount > 0.f
                ? ImGuiWindowFlags_NoScrollWithMouse
                : ImGuiWindowFlags_None);
        ImGui::BeginChild(
            "##SettingsBody",
            ImVec2(0.f, 0.f),
            ImGuiChildFlags_AutoResizeY,
            settingsBodyFlags);
        ImGuiWindow* settingsBodyWindow =
            ImGui::GetCurrentWindow();
        const SettingsScrollStabilityContext& previousScrollContext =
            g_SettingsScrollStabilityContext;
        const bool settingsScrolledThisFrame =
            previousScrollContext.lastFrame ==
                ImGui::GetFrameCount() - 1 &&
            std::abs(
                settingsBodyWindow->Scroll.y -
                    previousScrollContext.lastScrollY) > 0.01f;
        if (settingsScrolledThisFrame)
            ImGui::CloseUvsrColorPickerPopup();
        const float colorPickerMaximumBottom = std::min(
            panelStackMaximumBottom,
            settingsBodyWindow->ParentWindow->Pos.y +
                settingsBodyWindow->ParentWindow->Size.y);
        ImVec4 colorPickerContentLayer =
            g_UiVisualTokens.drawerBackground;
        colorPickerContentLayer.w *= 0.72f;
        ImVec4 colorPickerControlLayer =
            g_UiVisualTokens.drawerBackground;
        ImGui::PushUvsrColorPickerPopupContentRight(
            settingsBodyWindow->InnerRect.Max.x,
            colorPickerMaximumBottom,
            g_UiVisualTokens.colorPickerSurface,
            g_UiVisualTokens.panelInsetFrame,
            colorPickerContentLayer,
            colorPickerControlLayer);
        ImDrawList* settingsBodyDrawList =
            settingsBodyWindow->DrawList;
        TrackSettingsAppearanceDrawList(settingsBodyDrawList);
        BeginSettingsScrollStability();

        // Keep the panel visually unchanged while a selection waits for its
        // stable commit frame. BeginDisabled blocks another mutation but, in
        // contrast to NoInputs, the hovered ImGui window continues capturing
        // the mouse so clicks and cursor motion cannot leak to the camera.
        const bool deferredDropdownInputBlocked =
            HasDeferredDropdownUiActions();
        if (deferredDropdownInputBlocked)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f);
            ImGui::PushUvsrDisabledPresentation(0.f);
            ImGui::BeginDisabled();
        }
        // A same-frame anchor correction translates submitted vertices after
        // item hit rectangles have been resolved. Keep widgets noninteractive
        // on every continuation/finalization frame of that motion so rendered
        // controls and hit testing can never disagree.
        const bool settingsScrollInputBlocked =
            uiMotionEnabled &&
            g_SettingsScrollStabilityContext.layoutAnimatingLastFrame;
        if (settingsScrollInputBlocked)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f);
            ImGui::PushUvsrDisabledPresentation(0.f);
            ImGui::BeginDisabled();
        }

        DrawGeneralDrawer(settingsControlWidth);

        if (BeginAnimatedToggleRegion(
                "##PathingDrawerVisibility",
                m_ui.Lighting == LightingSolution::PathTracing))
        {
            DrawPathingDrawer(settingsControlWidth);
            EndAnimatedToggleRegion();
        }

        const bool representationOpen = DrawCollapsingHeader(
            "Representation",
            "Configure the world space hierarchy shared by ray traced "
            "techniques.");
        if (representationOpen)
        {
            BeginDrawerBody(
                "##RepresentationBody",
                settingsControlWidth);

            WorldSpaceRepresentationSettings& representation =
                m_ui.Representation;
            const WorldSpaceRepresentationSettings representationDefaults{};
            const WorldSpaceRepresentationStatus& representationStatus =
                m_app->GetWorldSpaceRepresentationStatus();
            if (ImGui::Checkbox(
                    "Allow Ray Traversal",
                    &representation.allowRayTraversal))
            {
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Allow ray traced effects to traverse the shared scene "
                "representation. Their settings stay stored while traversal "
                "is off.");
            if (DrawPresetResetIcon(
                    "RepresentationAllowRayTraversal",
                    representation.allowRayTraversal !=
                        representationDefaults.allowRayTraversal))
            {
                representation.allowRayTraversal =
                    representationDefaults.allowRayTraversal;
                m_app->ResetImageBasedLightingHistory();
            }

            const char* representationState = "Inactive";
            switch (representationStatus.state)
            {
            case WorldSpaceRepresentationState::Unsupported:
                representationState = "Unsupported";
                break;
            case WorldSpaceRepresentationState::BuildingBlas:
                representationState = "Building BLAS";
                break;
            case WorldSpaceRepresentationState::BuildingTlas:
                representationState = "Building TLAS";
                break;
            case WorldSpaceRepresentationState::Ready:
                representationState = "Ready";
                break;
            case WorldSpaceRepresentationState::Failed:
                representationState = "Failed";
                break;
            case WorldSpaceRepresentationState::Idle:
            default:
                break;
            }
            if (!representation.allowRayTraversal)
            {
                ImGui::TextDisabled("Status: Ray traversal disabled");
            }
            else
                ImGui::TextDisabled("Status: %s", representationState);
            if (!representationStatus.accelerationStructuresSupported ||
                !representationStatus.rayQueriesSupported)
            {
                ImGui::TextDisabled(
                    "Requires DirectX Raytracing 1.1 inline ray queries.");
            }

            if (BeginAnimatedTreeNode(
                    "Bounding Volume Hierarchy##Representation",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure the shared world-space BVH family."))
            {
                static constexpr const char* BuildPreferenceLabels[] = {
                    "Fast Trace", "Balanced", "Fast Build"
                };
                const int buildPreferenceIndex = std::clamp(
                    int(representation.bvhBuildPreference),
                    0,
                    int(std::size(BuildPreferenceLabels)) - 1);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Build Preference##BVH",
                        BuildPreferenceLabels[buildPreferenceIndex]))
                {
                    for (int index = 0;
                        index < int(std::size(BuildPreferenceLabels));
                        ++index)
                    {
                        const BvhBuildPreference candidate =
                            BvhBuildPreference(index);
                        DrawDeferredDropdownOption(
                            BuildPreferenceLabels[index],
                            BuildPreferenceLabels[index],
                            representation.bvhBuildPreference == candidate,
                            [settings = &representation,
                                app = m_app,
                                candidate]()
                            {
                                const WorldSpaceRepresentationSettings before =
                                    *settings;
                                settings->bvhBuildPreference = candidate;
                                app->InvalidateWorldSpaceRepresentation(
                                    GetWorldSpaceRepresentationInvalidation(
                                        before, *settings));
                            });
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose whether BVH construction prioritizes ray traversal, "
                    "balanced work, or construction speed.");
                if (DrawNestedDropdownResetIcon(
                        "RepresentationBvhBuildPreference",
                        representation.bvhBuildPreference !=
                            representationDefaults.bvhBuildPreference))
                {
                    QueueDeferredControlUiAction(
                        [settings = &representation,
                            app = m_app,
                            defaultValue = representationDefaults
                                .bvhBuildPreference]()
                        {
                            const WorldSpaceRepresentationSettings before =
                                *settings;
                            settings->bvhBuildPreference = defaultValue;
                            app->InvalidateWorldSpaceRepresentation(
                                GetWorldSpaceRepresentationInvalidation(
                                    before, *settings));
                        });
                }
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Bottom Level Acceleration Structures##Representation",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure per-mesh triangle acceleration structures."))
            {
                static constexpr const char* BlasUpdateLabels[] = {
                    "Rebuild", "Refit"
                };
                const int updateIndex = std::clamp(
                    int(representation.blasUpdateMode),
                    0,
                    int(std::size(BlasUpdateLabels)) - 1);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Dynamic Updates##BLAS",
                        BlasUpdateLabels[updateIndex]))
                {
                    for (int index = 0;
                        index < int(std::size(BlasUpdateLabels));
                        ++index)
                    {
                        const BlasUpdateMode candidate =
                            BlasUpdateMode(index);
                        DrawDeferredDropdownOption(
                            BlasUpdateLabels[index],
                            BlasUpdateLabels[index],
                            representation.blasUpdateMode == candidate,
                            [settings = &representation,
                                app = m_app,
                                candidate]()
                            {
                                const WorldSpaceRepresentationSettings before =
                                    *settings;
                                settings->blasUpdateMode = candidate;
                                app->InvalidateWorldSpaceRepresentation(
                                    GetWorldSpaceRepresentationInvalidation(
                                        before, *settings));
                            });
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Rebuild or refit changed skinned-mesh BLAS geometry.");
                if (DrawNestedDropdownResetIcon(
                        "RepresentationBlasUpdateMode",
                        representation.blasUpdateMode !=
                            representationDefaults.blasUpdateMode))
                {
                    QueueDeferredControlUiAction(
                        [settings = &representation,
                            app = m_app,
                            defaultValue = representationDefaults
                                .blasUpdateMode]()
                        {
                            const WorldSpaceRepresentationSettings before =
                                *settings;
                            settings->blasUpdateMode = defaultValue;
                            app->InvalidateWorldSpaceRepresentation(
                                GetWorldSpaceRepresentationInvalidation(
                                    before, *settings));
                        });
                }
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Top Level Acceleration Structure##Representation",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure the instance hierarchy consumed by ray queries."))
            {
                static constexpr const char* TlasUpdateLabels[] = {
                    "Rebuild", "Refit"
                };
                const int updateIndex = std::clamp(
                    int(representation.tlasUpdateMode),
                    0,
                    int(std::size(TlasUpdateLabels)) - 1);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Transform Updates##TLAS",
                        TlasUpdateLabels[updateIndex]))
                {
                    for (int index = 0;
                        index < int(std::size(TlasUpdateLabels));
                        ++index)
                    {
                        const TlasUpdateMode candidate =
                            TlasUpdateMode(index);
                        DrawDeferredDropdownOption(
                            TlasUpdateLabels[index],
                            TlasUpdateLabels[index],
                            representation.tlasUpdateMode == candidate,
                            [settings = &representation,
                                app = m_app,
                                candidate]()
                            {
                                const WorldSpaceRepresentationSettings before =
                                    *settings;
                                settings->tlasUpdateMode = candidate;
                                app->InvalidateWorldSpaceRepresentation(
                                    GetWorldSpaceRepresentationInvalidation(
                                        before, *settings));
                            });
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Rebuild or refit the TLAS when instance transforms change.");
                if (DrawNestedDropdownResetIcon(
                        "RepresentationTlasUpdateMode",
                        representation.tlasUpdateMode !=
                            representationDefaults.tlasUpdateMode))
                {
                    QueueDeferredControlUiAction(
                        [settings = &representation,
                            app = m_app,
                            defaultValue = representationDefaults
                                .tlasUpdateMode]()
                        {
                            const WorldSpaceRepresentationSettings before =
                                *settings;
                            settings->tlasUpdateMode = defaultValue;
                            app->InvalidateWorldSpaceRepresentation(
                                GetWorldSpaceRepresentationInvalidation(
                                    before, *settings));
                        });
                }
                EndAnimatedTreeNode();
            }

            EndDrawerBody();
        }
        ImGui::Spacing();

        const auto drawNoiseSettingsControls = [&] (
            NoiseSettings& settings,
            const NoiseSettings& defaults,
            const char* identifier,
            bool nestedResetIcons)
        {
            bool changed = false;
            static constexpr const char* PatternLabels[] = {
                "Spatial White",
                "Spatial Blue",
                "Spatiotemporal Blue"
            };
            static constexpr const char* ResolutionLabels[] = {
                "64x64",
                "128x128",
                "256x256",
                "512x512"
            };
            static constexpr NoiseResolution Resolutions[] = {
                NoiseResolution::Size64,
                NoiseResolution::Size128,
                NoiseResolution::Size256,
                NoiseResolution::Size512
            };

            const std::string patternLabel =
                std::string("Noise Pattern##") + identifier;
            int patternIndex = std::clamp(
                static_cast<int>(settings.pattern),
                0,
                static_cast<int>(std::size(PatternLabels)) - 1);
            SetNextLabeledControlWidth("Noise Pattern", settingsControlWidth);
            if (ImGui::Combo(
                    patternLabel.c_str(),
                    &patternIndex,
                    PatternLabels,
                    static_cast<int>(std::size(PatternLabels))))
            {
                settings.pattern = static_cast<NoisePattern>(patternIndex);
                changed = true;
            }
            ImGui::SetItemTooltip(
                "Choose a precomputed R8 spatial or spatiotemporal noise "
                "texture.");
            const std::string patternReset =
                std::string(identifier) + "NoisePattern";
            const bool resetPattern = nestedResetIcons
                ? DrawNestedDropdownResetIcon(
                    patternReset.c_str(),
                    settings.pattern != defaults.pattern)
                : DrawPresetResetIcon(
                    patternReset.c_str(),
                    settings.pattern != defaults.pattern);
            if (resetPattern)
            {
                settings.pattern = defaults.pattern;
                changed = true;
            }

            int resolutionIndex = 0;
            for (int index = 0;
                index < static_cast<int>(std::size(Resolutions));
                ++index)
            {
                if (settings.resolution == Resolutions[index])
                    resolutionIndex = index;
            }
            const std::string resolutionLabel =
                std::string("Noise Resolution##") + identifier;
            SetNextLabeledControlWidth(
                "Noise Resolution", settingsControlWidth);
            if (ImGui::Combo(
                    resolutionLabel.c_str(),
                    &resolutionIndex,
                    ResolutionLabels,
                    static_cast<int>(std::size(ResolutionLabels))))
            {
                settings.resolution = Resolutions[resolutionIndex];
                changed = true;
            }
            ImGui::SetItemTooltip(
                "Choose the centered tile resolution used by this noise "
                "texture.");
            const std::string resolutionReset =
                std::string(identifier) + "NoiseResolution";
            const bool resetResolution = nestedResetIcons
                ? DrawNestedDropdownResetIcon(
                    resolutionReset.c_str(),
                    settings.resolution != defaults.resolution)
                : DrawPresetResetIcon(
                    resolutionReset.c_str(),
                    settings.resolution != defaults.resolution);
            if (resetResolution)
            {
                settings.resolution = defaults.resolution;
                changed = true;
            }

            const std::string animateLabel =
                std::string("Animate Samples##") + identifier;
            if (ImGui::Checkbox(animateLabel.c_str(), &settings.animate))
                changed = true;
            ImGui::SetItemTooltip(
                "Advance the noise sequence after each successful effect "
                "dispatch.");
            const std::string animateReset =
                std::string(identifier) + "NoiseAnimate";
            if (DrawPresetResetIcon(
                    animateReset.c_str(),
                    settings.animate != defaults.animate))
            {
                settings.animate = defaults.animate;
                changed = true;
            }
            return changed;
        };

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
                if (ImGui::Checkbox(
                        "Enable##SampleAccumulation",
                        &m_ui.AccumulateSamples))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Average each successful Ray Marching sample until camera, scene, resolution, or lighting changes.");
            };

            const NoiseSettings defaults;
            if (drawNoiseSettingsControls(
                    m_ui.Noise,
                    defaults,
                    "GlobalNoise",
                    false))
            {
                m_app->ResetNoiseSamplingHistory(
                    !m_ui.ScreenSpaceVisibility.noise.specifyNoise,
                    false,
                    !m_ui.RayTracedSkyVisibility.noise.specifyNoise,
                    true);
            }
            ImGui::TextDisabled(
                "Resident texture memory: %.2f MiB",
                double(m_app->GetNoiseTextureResidentBytes()) /
                    (1024.0 * 1024.0));
            if (BeginAnimatedTreeNode(
                    "Accumulate Samples##Noise",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Average finite scene-linear samples while the camera, "
                    "scene, and lighting remain still."))
            {
                drawSampleAccumulationControls();
                EndAnimatedTreeNode();
            }
            EndDrawerBody();
        }
        ImGui::Spacing();

        const auto drawDirectionalRayShadowControls = [&]()
        {
            if (!BeginAnimatedTreeNode(
                    "Ray Traced Shadows##Shadows",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Trace one direct binary sun-visibility ray per raster sample."))
            {
                return;
            }

            DirectionalShadowSettings& settings = m_ui.DirectionalShadows;
            const DirectionalShadowSettings defaults;
            const bool available = m_app->HasPrimaryDirectionalLight() &&
                m_app->SupportsDirectionalRayVisibility();
            ImGui::BeginDisabled(!available && !settings.enabled);
            if (ImGui::Checkbox("Enabled##DirectionalRayShadows", &settings.enabled))
                m_app->ResetImageBasedLightingHistory();
            ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                "Trace direct directional-light visibility independently for every 1x, 2x, 4x, 8x, or 16x raster sample.");
            if (DrawPresetResetIcon(
                    "DirectionalRayShadowsEnabled",
                    settings.enabled != defaults.enabled))
            {
                settings.enabled = defaults.enabled;
                m_app->ResetImageBasedLightingHistory();
            }

            if (BeginAnimatedToggleRegion(
                    "##DirectionalRayShadowControls", settings.enabled))
            {
                static constexpr std::array<std::pair<const char*,
                    RayVisibilityMaxDistance>, 6> Distances = {{
                        { "Maximum", RayVisibilityMaxDistance::Maximum },
                        { "32 m", RayVisibilityMaxDistance::Meters32 },
                        { "16 m", RayVisibilityMaxDistance::Meters16 },
                        { "8 m", RayVisibilityMaxDistance::Meters8 },
                        { "4 m", RayVisibilityMaxDistance::Meters4 },
                        { "2 m", RayVisibilityMaxDistance::Meters2 }
                    }};
                const auto distance = std::find_if(
                    Distances.begin(), Distances.end(),
                    [&settings](const auto& candidate)
                    {
                        return candidate.second == settings.maxDistance;
                    });
                SetNextLabeledControlWidth("Maximum Distance", settingsControlWidth);
                if (BeginRoundedCombo(
                        "Maximum Distance",
                        distance != Distances.end() ? distance->first : "Maximum"))
                {
                    for (const auto& [label, value] : Distances)
                    {
                        DrawDeferredDropdownOption(
                            label, label, settings.maxDistance == value,
                            [this, value]()
                            {
                                m_ui.DirectionalShadows.maxDistance = value;
                                m_app->ResetImageBasedLightingHistory();
                            });
                    }
                    ImGui::EndCombo();
                }
                if (DrawBoundedSliderFloat(
                        "Ray Bias", &settings.rayBias, 0.f,
                        DirectionalShadowMaximumRayBias, 0.f, 0.02f, "%.4f"))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::TextDisabled(
                    "Direct visibility: %ux receiver samples.",
                    m_app->GetActiveRasterSampleCount());
                EndAnimatedToggleRegion();
            }
            if (!available)
                ImGui::TextDisabled("DXR 1.1 and a primary directional light are required.");
            EndAnimatedTreeNode();
        };

        if (BeginAnimatedToggleRegion(
                "##DiffuseDrawerVisibility",
                m_ui.Lighting == LightingSolution::RayMarching,
                UiToggleRegionOwner::Settings,
                UiToggleRegionVisualMode::ClipDuringCollapse))
        {
        const bool diffuseOpen = DrawCollapsingHeader(
            "Diffuse",
            "Configure occlusion, illumination, sampling, "
            "automatic upsampling, and buffer precision.");
        if (diffuseOpen)
        {
            BeginDrawerBody("##DiffuseBody", settingsControlWidth);
            ScreenSpaceVisibilitySettings& visibility =
                m_ui.ScreenSpaceVisibility;
            const auto finishVisibilityEdit =
                [this](ScreenSpaceVisibilitySettings& settings)
            {
                MarkScreenSpaceVisibilityQualityCustom(settings);
                ReconcileScreenSpaceVisibilityQualityPreset(settings);
                m_app->ResetImageBasedLightingHistory();
            };
            ScreenSpaceVisibilityQuality profileOrigin =
                visibility.quality == ScreenSpaceVisibilityQuality::Custom
                ? visibility.qualityPresetOrigin
                : visibility.quality;
            if (profileOrigin == ScreenSpaceVisibilityQuality::Custom)
                profileOrigin = ScreenSpaceVisibilityQuality::High;
            ScreenSpaceVisibilitySettings profileDefaults = visibility;
            ApplyScreenSpaceVisibilityQualityPreset(
                profileDefaults, profileOrigin);

            if (ImGui::Checkbox("Enabled", &visibility.enabled))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Enable screen space occlusion and illumination. "
                "Other lighting and material effects remain independent.");
            if (DrawPresetResetIcon(
                    "VisibilityEnabled",
                    visibility.enabled != profileDefaults.enabled))
            {
                visibility.enabled = profileDefaults.enabled;
                finishVisibilityEdit(visibility);
            }

            static constexpr const char* QualityLabels[] = {
                "Low", "Medium", "High", "Ultra"
            };
            const int profileIndex = std::clamp(
                static_cast<int>(profileOrigin),
                0,
                static_cast<int>(std::size(QualityLabels)) - 1);
            std::string profilePreview = QualityLabels[profileIndex];
            if (visibility.quality ==
                ScreenSpaceVisibilityQuality::Custom)
            {
                profilePreview += " (Custom)";
            }
            SetNextLabeledControlWidth(
                "Profile##Visibility", settingsControlWidth);
            if (BeginRoundedCombo(
                    "Profile##Visibility",
                    profilePreview.c_str()))
            {
                static constexpr const char* ProfileTooltips[] = {
                    "Quarter-resolution Bitmask Approximation: 8 samples, shared Noise, automatic guide-aware upsampling, 16-bit buffers.",
                    "Half-resolution Bitmask Directional Visibility: 8 samples, shared Noise, automatic guide-aware upsampling, 16-bit buffers.",
                    "Full resolution Bitmask Directional Visibility with sixteen samples, the shared Noise configuration, and 16 bit buffers.",
                    "Full-resolution Bitmask Directional Visibility, 48 samples, shared Noise, and 32-bit buffers."
                };
                for (int index = 0;
                    index < static_cast<int>(std::size(QualityLabels));
                    ++index)
                {
                    const auto selected =
                        static_cast<ScreenSpaceVisibilityQuality>(index);
                    DrawDeferredDropdownOption(
                        QualityLabels[index],
                        QualityLabels[index],
                        visibility.quality == selected,
                        [this, settings = &visibility, selected]()
                        {
                            ApplyScreenSpaceVisibilityQualityPreset(
                                *settings, selected);
                            m_app->ResetImageBasedLightingHistory();
                        });
                    ImGui::SetItemTooltip("%s", ProfileTooltips[index]);
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose a diffuse profile. Changes retain it and append "
                "(Custom); the circular arrow restores the complete High "
                "profile.");
            if (DrawPresetResetIcon(
                    "VisibilityProfile",
                    !MatchesScreenSpaceVisibilityQualityPreset(
                        visibility, ScreenSpaceVisibilityQuality::High),
                    "Restore the complete High profile."))
            {
                QueueDeferredControlUiAction(
                    [this, settings = &visibility]()
                    {
                        ApplyScreenSpaceVisibilityQualityPreset(
                            *settings,
                            ScreenSpaceVisibilityQuality::High);
                        m_app->ResetImageBasedLightingHistory();
                    });
            }

            if (BeginAnimatedToggleRegion(
                    "##VisibilityControls", visibility.enabled))
            {

            if (BeginAnimatedTreeNode(
                    "Occlusion###Ambient Occlusion##Visibility",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure contact darkening from nearby geometry."))
            {
            if (ImGui::Checkbox(
                    "Enabled##VisibilityAmbient",
                    &visibility.ambientOcclusion.enabled))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Darken surfaces whose nearby geometry blocks ambient light.");
            if (DrawPresetResetIcon(
                    "VisibilityAmbientEnabled",
                    visibility.ambientOcclusion.enabled !=
                        profileDefaults.ambientOcclusion.enabled))
            {
                visibility.ambientOcclusion.enabled =
                    profileDefaults.ambientOcclusion.enabled;
                finishVisibilityEdit(visibility);
            }
            if (BeginAnimatedToggleRegion(
                    "##VisibilityAmbientControls",
                    visibility.ambientOcclusion.enabled))
            {
            if (DrawBoundedSliderFloat(
                    "Strength",
                    &visibility.ambientOcclusion.strength,
                    MinimumVisibilityAmbientOcclusionStrength,
                    MaximumVisibilityAmbientOcclusionStrength,
                    MinimumVisibilityAmbientOcclusionStrength,
                    2.f,
                    "%.2f"))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Scale how strongly nearby occluders darken the final image.");
            if (DrawPresetResetIcon(
                    "VisibilityAmbientStrength",
                    visibility.ambientOcclusion.strength !=
                        profileDefaults.ambientOcclusion.strength))
            {
                visibility.ambientOcclusion.strength =
                    profileDefaults.ambientOcclusion.strength;
                finishVisibilityEdit(visibility);
            }
            if (ImGui::Checkbox(
                    "Output Hit Distance##VisibilityAmbient",
                    &visibility.ambientOcclusion.outputHitDistance))
            {
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Output a physical blocker distance for ambient-occlusion "
                "denoising. The distance path is omitted while off.");
            if (DrawPresetResetIcon(
                    "VisibilityAmbientOutputHitDistance",
                    visibility.ambientOcclusion.outputHitDistance))
            {
                visibility.ambientOcclusion.outputHitDistance =
                    false;
                m_app->ResetImageBasedLightingHistory();
            }
            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Illumination###Indirect Diffuse##Visibility",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure diffuse light reflected from visible surfaces."))
            {
            if (ImGui::Checkbox(
                    "Enabled##VisibilityIndirect",
                    &visibility.indirectDiffuse.enabled))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Add diffuse light reflected from nearby visible surfaces.");
            if (DrawPresetResetIcon(
                    "VisibilityIndirectEnabled",
                    visibility.indirectDiffuse.enabled !=
                        profileDefaults.indirectDiffuse.enabled))
            {
                visibility.indirectDiffuse.enabled =
                    profileDefaults.indirectDiffuse.enabled;
                finishVisibilityEdit(visibility);
            }
            if (BeginAnimatedToggleRegion(
                    "##VisibilityIndirectControls",
                    visibility.indirectDiffuse.enabled))
            {
            if (DrawBoundedSliderFloat(
                    "Intensity",
                    &visibility.indirectDiffuse.intensity,
                    0.f,
                    16.f,
                    0.f,
                    4.f,
                    "%.2f"))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Scale the diffuse light gathered from nearby surfaces.");
            if (DrawPresetResetIcon(
                    "VisibilityIndirectIntensity",
                    visibility.indirectDiffuse.intensity !=
                        profileDefaults.indirectDiffuse.intensity))
            {
                visibility.indirectDiffuse.intensity =
                    profileDefaults.indirectDiffuse.intensity;
                finishVisibilityEdit(visibility);
            }
            if (ImGui::Checkbox(
                    "Output Hit Distance##VisibilityIndirect",
                    &visibility.indirectDiffuse.outputHitDistance))
            {
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Output a physical hit distance for diffuse-illumination "
                "denoising. The distance path is omitted while off.");
            if (DrawPresetResetIcon(
                    "VisibilityIndirectOutputHitDistance",
                    visibility.indirectDiffuse.outputHitDistance))
            {
                visibility.indirectDiffuse.outputHitDistance =
                    false;
                m_app->ResetImageBasedLightingHistory();
            }
            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Sampling##Visibility",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure where and how visibility rays sample the scene."))
            {
            static constexpr const char* ResolutionLabels[] = {
                "Full Resolution", "Half Resolution", "Quarter Resolution"
            };
            int resolution = static_cast<int>(visibility.resolution);
            SetNextLabeledControlWidth(
                "Sampling Resolution", settingsControlWidth);
            if (ImGui::Combo(
                    "Sampling Resolution", &resolution, ResolutionLabels,
                    static_cast<int>(std::size(ResolutionLabels))))
            {
                visibility.resolution =
                    static_cast<VisibilityResolution>(resolution);
                finishVisibilityEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Choose the resolution used for visibility tracing. Reduced "
                "resolutions are reconstructed before composition.");
            if (DrawNestedDropdownResetIcon(
                    "VisibilityResolution",
                    visibility.resolution != profileDefaults.resolution))
            {
                visibility.resolution = profileDefaults.resolution;
                finishVisibilityEdit(visibility);
            }

            static constexpr const char* EstimatorLabels[] = {
                "Bitmask Approximation",
                "Bitmask Directional Visibility",
                "Bitmask Cosine Visibility"
            };
            int estimator = static_cast<int>(visibility.estimator);
            SetNextLabeledControlWidth(
                "Estimator", settingsControlWidth);
            if (ImGui::Combo(
                    "Estimator", &estimator, EstimatorLabels,
                    static_cast<int>(std::size(EstimatorLabels))))
            {
                visibility.estimator =
                    static_cast<VisibilityEstimator>(estimator);
                finishVisibilityEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Choose how directions are distributed around each receiver.");
            if (DrawNestedDropdownResetIcon(
                    "VisibilityEstimator",
                    visibility.estimator != profileDefaults.estimator))
            {
                visibility.estimator = profileDefaults.estimator;
                finishVisibilityEdit(visibility);
            }

            const NoiseSettings oldResolvedNoise = ResolveNoiseSettings(
                m_ui.Noise,
                visibility.noise);
            bool noiseOverrideChanged = false;
            if (ImGui::Checkbox(
                    "Specify Noise##Visibility",
                    &visibility.noise.specifyNoise))
            {
                noiseOverrideChanged = true;
            }
            ImGui::SetItemTooltip(
                "Use custom noise sampling for this effect only. This does "
                "not change the noise sampling used by any other effect.");
            if (DrawPresetResetIcon(
                    "VisibilitySpecifyNoise",
                    visibility.noise.specifyNoise !=
                        profileDefaults.noise.specifyNoise))
            {
                visibility.noise.specifyNoise =
                    profileDefaults.noise.specifyNoise;
                noiseOverrideChanged = true;
            }
            if (BeginAnimatedToggleRegion(
                    "##VisibilityCustomNoise",
                    visibility.noise.specifyNoise))
            {
                noiseOverrideChanged |= drawNoiseSettingsControls(
                    visibility.noise.custom,
                    profileDefaults.noise.custom,
                    "Visibility",
                    true);
                EndAnimatedToggleRegion();
            }
            const NoiseSettings newResolvedNoise = ResolveNoiseSettings(
                m_ui.Noise,
                visibility.noise);
            if (noiseOverrideChanged && oldResolvedNoise != newResolvedNoise)
            {
                m_app->ResetNoiseSamplingHistory(
                    true, false, false, false);
            }

            int samples =
                static_cast<int>(visibility.sampling.maximumSampleCount);
            if (DrawBoundedSliderInt(
                    "Samples",
                    &samples,
                    1,
                    64,
                    1,
                    48))
            {
                visibility.sampling.maximumSampleCount =
                    static_cast<uint32_t>(samples);
                finishVisibilityEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Set the number of visibility samples traced per pixel.");
            if (DrawPresetResetIcon(
                    "VisibilitySamples",
                    visibility.sampling.maximumSampleCount !=
                        profileDefaults.sampling.maximumSampleCount))
            {
                visibility.sampling.maximumSampleCount =
                    profileDefaults.sampling.maximumSampleCount;
                finishVisibilityEdit(visibility);
            }
            if (DrawBoundedSliderFloat(
                    "Radius", &visibility.sampling.radius,
                    0.1f, 10.f,
                    0.1f, 6.f,
                    "%.2f"))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Set the world-space reach of nearby visibility samples.");
            if (DrawPresetResetIcon(
                    "VisibilityRadius",
                    visibility.sampling.radius !=
                        profileDefaults.sampling.radius))
            {
                visibility.sampling.radius =
                    profileDefaults.sampling.radius;
                finishVisibilityEdit(visibility);
            }
            if (DrawBoundedSliderFloat(
                    "Thickness", &visibility.sampling.thickness,
                    0.01f, 2.f,
                    0.01f, 1.f,
                    "%.2f"))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Set the accepted thickness of potential occluding surfaces.");
            if (DrawPresetResetIcon(
                    "VisibilityThickness",
                    visibility.sampling.thickness !=
                        profileDefaults.sampling.thickness))
            {
                visibility.sampling.thickness =
                    profileDefaults.sampling.thickness;
                finishVisibilityEdit(visibility);
            }
            if (DrawBoundedSliderFloat(
                    "Distribution",
                    &visibility.sampling.stepDistributionExponent,
                    MinimumVisibilityStepDistributionExponent,
                    MaximumVisibilityStepDistributionExponent,
                    MinimumVisibilityStepDistributionExponent,
                    4.f,
                    "%.2f"))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Bias samples toward the receiver or toward the trace edge.");
            if (DrawPresetResetIcon(
                    "VisibilityDistribution",
                    visibility.sampling.stepDistributionExponent !=
                        profileDefaults.sampling.stepDistributionExponent))
            {
                visibility.sampling.stepDistributionExponent =
                    profileDefaults.sampling.stepDistributionExponent;
                finishVisibilityEdit(visibility);
            }
            EndAnimatedTreeNode();
            }


            EndAnimatedToggleRegion();
            }

            EndDrawerBody();
        }
        EndAnimatedToggleRegion();
        }
        const bool denoisingOpen = DrawCollapsingHeader(
            "Denoising",
            m_ui.Lighting == LightingSolution::PathTracing
                ? "The standard path tracer presents its cumulative raw mean."
                : "Choose raw output, a built-in bilateral filter, or a "
                  "configured third-party denoiser for each effect.");
        if (denoisingOpen)
        {
            BeginDrawerBody("##DenoisingBody", settingsControlWidth);
            if (BeginAnimatedToggleRegion(
                    "##RayMarchingDenoisingBody",
                    m_ui.Lighting == LightingSolution::RayMarching))
            {
            const auto drawDenoisingSignal =
                [this, settingsControlWidth](
                    const char* treeLabel,
                    const char* identifier,
                    const char* description,
                    DenoisingEffect effect,
                    DenoisingSignalSettings& signal)
            {
                if (!BeginAnimatedTreeNode(
                        treeLabel,
                        ImGuiTreeNodeFlags_DefaultOpen,
                        description))
                {
                    return;
                }

                const DenoisingSignalSettings defaults;
                const std::string methodLabel =
                    std::string("Method##Denoising") + identifier;
                SetNextLabeledControlWidth(
                    methodLabel.c_str(), settingsControlWidth);
                if (BeginRoundedCombo(
                        methodLabel.c_str(),
                        GetDenoisingMethodLabel(signal.method)))
                {
                    static constexpr std::array<DenoisingMethodChoice, 6>
                        Methods = {
                            DenoisingMethodChoice::None,
                            DenoisingMethodChoice::JointBilateral,
                            DenoisingMethodChoice::GaussianBilateral,
                            DenoisingMethodChoice::Reblur,
                            DenoisingMethodChoice::Relax,
                            DenoisingMethodChoice::Sigma
                        };
                    for (const DenoisingMethodChoice method : Methods)
                    {
                        if (!SupportsDenoisingMethod(effect, method))
                            continue;
                        DenoisingSignalSettings* const selected = &signal;
                        DrawDeferredDropdownOption(
                            GetDenoisingMethodLabel(method),
                            GetDenoisingMethodLabel(method),
                            signal.method == method,
                            [this, selected, method]()
                            {
                                selected->method = method;
                                m_app->ResetImageBasedLightingHistory();
                            });
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Raw preserves the effect output. Joint Bilateral and "
                    "Gaussian Bilateral are built in; configured third-party "
                    "methods expose their additional controls.");
                const std::string resetIdentifier =
                    std::string("DenoisingSignal") + identifier;
                if (DrawPresetResetIcon(
                        resetIdentifier.c_str(),
                        signal != defaults,
                        "Restore this signal to its default settings."))
                {
                    signal = defaults;
                    m_app->ResetImageBasedLightingHistory();
                }

                if (BeginAnimatedToggleRegion(
                        (std::string("##DenoisingControls") + identifier)
                            .c_str(),
                        signal.method != DenoisingMethodChoice::None))
                {
                    if (IsSpatialDenoisingMethod(signal.method))
                    {
                        const std::string radiusLabel =
                            std::string("Radius##Denoising") + identifier;
                        if (DrawSliderFloat(
                                radiusLabel.c_str(),
                                &signal.spatialRadius,
                                1.f,
                                8.f,
                                "%.1f"))
                        {
                            m_app->ResetImageBasedLightingHistory();
                        }
                        ImGui::SetItemTooltip(
                            "Set the built-in spatial filter radius in pixels.");
                        if (DrawPresetResetIcon(
                                (std::string("DenoisingSpatialRadius") +
                                    identifier).c_str(),
                                signal.spatialRadius != defaults.spatialRadius))
                        {
                            signal.spatialRadius = defaults.spatialRadius;
                            m_app->ResetImageBasedLightingHistory();
                        }
                    }
                    if (IsThirdPartyDenoisingMethod(signal.method))
                    {
                    const std::string qualityLabel =
                        std::string("Quality##Denoising") + identifier;
                    SetNextLabeledControlWidth(
                        qualityLabel.c_str(), settingsControlWidth);
                    if (BeginRoundedCombo(
                            qualityLabel.c_str(),
                            GetDenoisingQualityLabel(signal.quality)))
                    {
                        static constexpr std::array<DenoisingQuality, 4>
                            Qualities = {
                                DenoisingQuality::Performance,
                                DenoisingQuality::Balanced,
                                DenoisingQuality::Quality,
                                DenoisingQuality::Ultra
                            };
                        for (const DenoisingQuality quality : Qualities)
                        {
                            DenoisingSignalSettings* const selected = &signal;
                            DrawDeferredDropdownOption(
                                GetDenoisingQualityLabel(quality),
                                GetDenoisingQualityLabel(quality),
                                signal.quality == quality,
                                [this, selected, quality]()
                                {
                                    selected->quality = quality;
                                    m_app->ResetImageBasedLightingHistory();
                                });
                        }
                        ImGui::EndCombo();
                    }
                    if (effect == DenoisingEffect::Shadows)
                    {
                        ImGui::SetItemTooltip(
                            "For shadows, Quality controls temporal stability. "
                            "Flashlight SIGMA is spatial only; Resolution "
                            "controls cost and detail.");
                    }
                    else
                    {
                        ImGui::SetItemTooltip(
                            "Performance is the fastest preset. Balanced is "
                            "the default; Quality and Ultra retain more "
                            "spatial and temporal detail.");
                    }

                    const std::string resolutionLabel =
                        std::string("Resolution##Denoising") + identifier;
                    SetNextLabeledControlWidth(
                        resolutionLabel.c_str(), settingsControlWidth);
                    if (BeginRoundedCombo(
                            resolutionLabel.c_str(),
                            GetDenoisingResolutionLabel(signal.resolution)))
                    {
                        static constexpr std::array<DenoisingResolution, 3>
                            Resolutions = {
                                DenoisingResolution::Quarter,
                                DenoisingResolution::Half,
                                DenoisingResolution::Full
                            };
                        for (const DenoisingResolution resolution : Resolutions)
                        {
                            DenoisingSignalSettings* const selected = &signal;
                            DrawDeferredDropdownOption(
                                GetDenoisingResolutionLabel(resolution),
                                GetDenoisingResolutionLabel(resolution),
                                signal.resolution == resolution,
                                [this, selected, resolution]()
                                {
                                    selected->resolution = resolution;
                                    m_app->ResetImageBasedLightingHistory();
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Choose the internal denoising resolution. Half is "
                        "the default balance of quality and cost.");

                    if (effect != DenoisingEffect::Shadows)
                    {
                        int historyLength =
                            static_cast<int>(signal.historyLength);
                        const std::string historyLabel =
                            std::string("History Length##Denoising") +
                            identifier;
                        if (DrawSliderInt(
                                historyLabel.c_str(),
                                &historyLength,
                                1,
                                32))
                        {
                            signal.historyLength =
                                static_cast<uint32_t>(historyLength);
                            m_app->ResetImageBasedLightingHistory();
                        }
                        ImGui::SetItemTooltip(
                            "Set the maximum number of frames retained by the "
                            "selected denoiser.");
                    }

                    const std::string disocclusionLabel =
                        std::string("Disocclusion##Denoising") + identifier;
                    if (DrawSliderFloat(
                            disocclusionLabel.c_str(),
                            &signal.disocclusionThreshold,
                            0.001f,
                            0.1f,
                            "%.3f"))
                    {
                        m_app->ResetImageBasedLightingHistory();
                    }
                    ImGui::SetItemTooltip(
                        effect == DenoisingEffect::Shadows
                            ? "Control sun SIGMA history rejection. "
                                "Flashlight SIGMA has no temporal history."
                            : "Control how readily history is rejected after "
                                "surface motion or visibility changes.");

                    if (effect != DenoisingEffect::Shadows)
                    {
                        const std::string antiLagLabel =
                            std::string("Response##Denoising") + identifier;
                        if (DrawSliderFloat(
                                antiLagLabel.c_str(),
                                &signal.antiLagStrength,
                                0.f,
                                1.f,
                                "%.2f"))
                        {
                            m_app->ResetImageBasedLightingHistory();
                        }
                        ImGui::SetItemTooltip(
                            "Increase responsiveness to sudden lighting "
                            "changes; lower values favor stability.");
                    }

                    }
                    EndAnimatedToggleRegion();
                }
                EndAnimatedTreeNode();
            };

            drawDenoisingSignal(
                "Occlusion###Ambient Occlusion##Denoising",
                "AmbientOcclusion",
                "Filter ambient occlusion with a built-in bilateral method "
                "or optional ReBLUR.",
                DenoisingEffect::AmbientOcclusion,
                m_ui.Denoising.ambientOcclusion);
            drawDenoisingSignal(
                "Illumination###Diffuse GI##Denoising",
                "DiffuseGi",
                "Filter indirect illumination with a built-in bilateral "
                "method or optional ReBLUR or ReLAX.",
                DenoisingEffect::DiffuseGi,
                m_ui.Denoising.diffuseGi);
            drawDenoisingSignal(
                "Shadows##Denoising",
                "Shadows",
                "Filter sun and flashlight visibility with a built-in "
                "bilateral method or optional SIGMA.",
                DenoisingEffect::Shadows,
                m_ui.Denoising.shadows);
            drawDenoisingSignal(
                "Sky Visibility##Denoising",
                "SkyVisibility",
                "Filter sky visibility with a built-in bilateral method or "
                "optional ReBLUR or ReLAX.",
                DenoisingEffect::SkyVisibility,
                m_ui.Denoising.skyVisibility);
            EndAnimatedToggleRegion();
            }

            EndDrawerBody();
        }
        ImGui::Spacing();
        if (BeginAnimatedToggleRegion(
                "##BuffersDrawerVisibility",
                m_ui.Lighting == LightingSolution::RayMarching,
                UiToggleRegionOwner::Settings,
                UiToggleRegionVisualMode::ClipDuringCollapse))
        {
        const bool buffersOpen = DrawCollapsingHeader(
            "Buffers",
            "Configure the retained visibility buffer precision controls.");
        if (buffersOpen)
        {
            BeginDrawerBody("##BuffersBody", settingsControlWidth);
            ScreenSpaceVisibilitySettings& visibility =
                m_ui.ScreenSpaceVisibility;
            ScreenSpaceVisibilityQuality profileOrigin =
                visibility.quality == ScreenSpaceVisibilityQuality::Custom
                ? visibility.qualityPresetOrigin
                : visibility.quality;
            if (profileOrigin == ScreenSpaceVisibilityQuality::Custom)
                profileOrigin = ScreenSpaceVisibilityQuality::High;
            ScreenSpaceVisibilitySettings profileDefaults = visibility;
            ApplyScreenSpaceVisibilityQualityPreset(
                profileDefaults, profileOrigin);
            const auto finishBufferEdit =
                [this](ScreenSpaceVisibilitySettings& settings)
            {
                MarkScreenSpaceVisibilityQualityCustom(settings);
                ReconcileScreenSpaceVisibilityQualityPreset(settings);
                m_app->ResetImageBasedLightingHistory();
            };

            const bool ambient16 =
                visibility.bufferPrecision.ambient ==
                    VisibilityScalarBufferPrecision::Float16;
            const bool indirect16 =
                visibility.bufferPrecision.indirect ==
                    VisibilityVectorBufferPrecision::Rgba16Float;
            const int bufferProfile = ambient16
                ? (indirect16 ? 0 : 2)
                : (indirect16 ? 3 : 1);
            static constexpr const char* BufferProfileLabels[] = {
                "Performance",
                "Maximum Precision",
                "Compact Occlusion",
                "Compact Indirect"
            };
            SetNextLabeledControlWidth(
                "Profile##Buffers", settingsControlWidth);
            if (BeginRoundedCombo(
                    "Profile##Buffers",
                    BufferProfileLabels[bufferProfile]))
            {
                static constexpr bool Ambient16[] = {
                    true, false, true, false
                };
                static constexpr bool Indirect16[] = {
                    true, false, false, true
                };
                static constexpr const char* BufferProfileTooltips[] = {
                    "Use 16-bit floating point for both retained visibility buffers.",
                    "Use 32-bit floating point for both retained visibility buffers.",
                    "Use 16-bit Occlusion and 32-bit Illumination.",
                    "Use 32-bit Occlusion and 16-bit Illumination."
                };
                for (int index = 0;
                    index < static_cast<int>(
                        std::size(BufferProfileLabels));
                    ++index)
                {
                    DrawDeferredDropdownOption(
                        BufferProfileLabels[index],
                        BufferProfileLabels[index],
                        index == bufferProfile,
                        [this, settings = &visibility, index]()
                        {
                            ApplyVisibilityBufferPrecisionPreset(
                                settings->bufferPrecision,
                                Ambient16[index],
                                Indirect16[index]);
                            MarkScreenSpaceVisibilityQualityCustom(*settings);
                            ReconcileScreenSpaceVisibilityQualityPreset(
                                *settings);
                            m_app->ResetImageBasedLightingHistory();
                        });
                    ImGui::SetItemTooltip(
                        "%s", BufferProfileTooltips[index]);
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose a compact precision combination for the two "
                "visibility buffers that remain in production.");
            const bool bufferProfileModified =
                visibility.bufferPrecision.ambient !=
                    profileDefaults.bufferPrecision.ambient ||
                visibility.bufferPrecision.indirect !=
                    profileDefaults.bufferPrecision.indirect;
            if (DrawPresetResetIcon(
                    "VisibilityBufferProfile",
                    bufferProfileModified,
                    "Restore both buffer precisions to the originating visibility profile."))
            {
                visibility.bufferPrecision =
                    profileDefaults.bufferPrecision;
                finishBufferEdit(visibility);
            }

            static constexpr const char* PrecisionLabels[] = {
                "16-Bit Floating Point", "32-Bit Floating Point"
            };
            int ambientPrecision = ambient16 ? 0 : 1;
            SetNextLabeledControlWidth(
                "Occlusion###Ambient Occlusion", settingsControlWidth);
            if (ImGui::Combo(
                    "Occlusion###Ambient Occlusion",
                    &ambientPrecision,
                    PrecisionLabels,
                    static_cast<int>(std::size(PrecisionLabels))))
            {
                visibility.bufferPrecision.ambient =
                    ambientPrecision == 0
                    ? VisibilityScalarBufferPrecision::Float16
                    : VisibilityScalarBufferPrecision::Float32;
                finishBufferEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Set the storage precision of the Occlusion field.");
            if (DrawPresetResetIcon(
                    "VisibilityAmbientPrecision",
                    visibility.bufferPrecision.ambient !=
                        profileDefaults.bufferPrecision.ambient))
            {
                visibility.bufferPrecision.ambient =
                    profileDefaults.bufferPrecision.ambient;
                finishBufferEdit(visibility);
            }

            int indirectPrecision = indirect16 ? 0 : 1;
            SetNextLabeledControlWidth(
                "Illumination###Indirect Diffuse", settingsControlWidth);
            if (ImGui::Combo(
                    "Illumination###Indirect Diffuse",
                    &indirectPrecision,
                    PrecisionLabels,
                    static_cast<int>(std::size(PrecisionLabels))))
            {
                visibility.bufferPrecision.indirect =
                    indirectPrecision == 0
                    ? VisibilityVectorBufferPrecision::Rgba16Float
                    : VisibilityVectorBufferPrecision::Rgba32Float;
                finishBufferEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Set the storage precision of the Illumination field.");
            if (DrawPresetResetIcon(
                    "VisibilityIndirectPrecision",
                    visibility.bufferPrecision.indirect !=
                        profileDefaults.bufferPrecision.indirect))
            {
                visibility.bufferPrecision.indirect =
                    profileDefaults.bufferPrecision.indirect;
                finishBufferEdit(visibility);
            }

            EndDrawerBody();
        }
        EndAnimatedToggleRegion();
        }
        if (BeginAnimatedToggleRegion(
                "##AliasingDrawerVisibility",
                m_ui.Lighting == LightingSolution::RayMarching,
                UiToggleRegionOwner::Settings,
                UiToggleRegionVisualMode::ClipDuringCollapse))
        {
        const bool antiAliasingOpen = DrawCollapsingHeader(
            "Aliasing",
            "Enable temporal, fast approximate, and multisample techniques "
            "independently.");
        if (antiAliasingOpen)
        {
            BeginDrawerBody("##AliasingBody", settingsControlWidth);
            ImGui::PushID("AliasingControls");
            AntiAliasingSettings& aliasing = m_ui.AntiAliasing;
            const AntiAliasingSettings aliasingDefaults{};

            const auto drawPresetEnum =
                [settingsControlWidth](const char* label,
                    auto value,
                    const char* const* labels,
                    int count,
                    bool custom,
                    auto applySelection)
                {
                    using Value = std::decay_t<decltype(value)>;
                    const int selected = std::clamp(
                        static_cast<int>(value), 0, count - 1);
                    std::string preview = labels[selected];
                    if (custom)
                        preview += " (Custom)";
                    SetNextLabeledControlWidth(
                        label, settingsControlWidth);
                    if (!BeginRoundedCombo(label, preview.c_str()))
                        return;
                    for (int index = 0; index < count; ++index)
                    {
                        const bool isSelected =
                            !custom && selected == index;
                        DrawDeferredDropdownOption(
                            labels[index],
                            labels[index],
                            isSelected,
                            [applySelection, index]() mutable
                            {
                                applySelection(
                                    static_cast<Value>(index));
                            });
                        if (selected == index)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                };

            static constexpr const char* QualityLabels[] = {
                "Low", "Medium", "High", "Ultra"
            };

            if (BeginAnimatedTreeNode(
                    "Temporal Reconstructive##Aliasing",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Reconstruct stable detail from current and previous frames."))
            {
            ImGui::Checkbox(
                "Enable##TemporalReconstructive",
                &aliasing.temporal.enabled);
            ImGui::SetItemTooltip(
                "Combine current and previous frames to reduce visible aliasing.");
            if (DrawPresetResetIcon(
                    "TemporalEnabled",
                    aliasing.temporal.enabled !=
                        aliasingDefaults.temporal.enabled))
            {
                aliasing.temporal.enabled =
                    aliasingDefaults.temporal.enabled;
            }
            if (BeginAnimatedToggleRegion(
                    "##TemporalReconstructiveControls",
                    aliasing.temporal.enabled))
            {
            const bool temporalQualityCustom =
                aliasing.temporal.nearestTexelDepth !=
                    aliasingDefaults.temporal.nearestTexelDepth ||
                !(aliasing.temporal.algorithmOverrides ==
                    aliasingDefaults.temporal.algorithmOverrides);
            const auto applyTemporalQualityPreset =
                [settings = &aliasing,
                    nearestTexelDepth =
                        aliasingDefaults.temporal.nearestTexelDepth,
                    algorithmOverrides =
                        aliasingDefaults.temporal.algorithmOverrides](
                    AntiAliasingQuality quality)
                {
                    settings->temporal.quality = quality;
                    settings->temporal.nearestTexelDepth =
                        nearestTexelDepth;
                    settings->temporal.algorithmOverrides =
                        algorithmOverrides;
                };
            drawPresetEnum(
                "Quality##TemporalReconstructive",
                aliasing.temporal.quality,
                QualityLabels,
                static_cast<int>(std::size(QualityLabels)),
                temporalQualityCustom,
                applyTemporalQualityPreset);
            ImGui::SetItemTooltip(
                "Choose Quality. Algorithm changes append (Custom); the arrow "
                "restores factory Quality and its Algorithm controls.");
            if (DrawPresetResetIcon(
                    "TemporalQuality",
                    aliasing.temporal.quality !=
                        aliasingDefaults.temporal.quality ||
                    temporalQualityCustom))
            {
                applyTemporalQualityPreset(
                    aliasingDefaults.temporal.quality);
            }

            static constexpr const char* CostLabels[] = {
                "Full Quality", "Reduced", "Minimum"
            };
            const bool temporalCostCustom =
                !(aliasing.temporal.behaviorOverrides ==
                    aliasingDefaults.temporal.behaviorOverrides) ||
                m_ui.TemporalAaSharpenEnabled ||
                m_ui.TemporalAaSharpness != TemporalAaDefaultSharpness;
            const auto applyTemporalCostPreset =
                [settings = &aliasing,
                    behaviorOverrides =
                        aliasingDefaults.temporal.behaviorOverrides,
                    ui = &m_ui](TemporalAaCostMode costMode)
                {
                    settings->temporal.costMode = costMode;
                    settings->temporal.behaviorOverrides =
                        behaviorOverrides;
                    ui->TemporalAaSharpenEnabled = false;
                    ui->TemporalAaSharpness =
                        TemporalAaDefaultSharpness;
                };
            ImGui::SetNextItemOpen(false, ImGuiCond_Once);
            if (BeginAnimatedTreeNode(
                    "Advanced##TemporalReconstructive",
                    ImGuiTreeNodeFlags_None,
                    "Override the temporal recipe. This section is closed by default."))
            {
                AntiAliasingSettings inheritedAliasing = aliasing;
                inheritedAliasing.temporal.algorithmOverrides = {};
                inheritedAliasing.temporal.behaviorOverrides = {};
                const ResolvedAntiAliasingSettings resolvedAliasing =
                    ResolveAntiAliasingSettings(inheritedAliasing);

                const auto drawAdvancedEnum =
                    [settingsControlWidth](
                        const char* label,
                        const char* resetId,
                        auto& value,
                        auto defaultValue,
                        int inheritedIndex,
                        const char* const* labels,
                        int count,
                        const char* tooltip,
                        const char* inheritedOption = nullptr)
                {
                    using Value = std::decay_t<decltype(value)>;
                    Value* setting = &value;
                    const int selectedValue = static_cast<int>(value);
                    const int selectedIndex = std::clamp(
                        selectedValue == 0
                            ? inheritedIndex
                            : selectedValue - 1,
                        0,
                        count - 1);
                    const char* preview =
                        selectedValue == 0 && inheritedOption
                            ? inheritedOption
                            : labels[selectedIndex];
                    SetNextLabeledControlWidth(label, settingsControlWidth);
                    if (BeginRoundedCombo(label, preview))
                    {
                        if (inheritedOption)
                        {
                            const bool selected = selectedValue == 0;
                            DrawDeferredDropdownOption(
                                inheritedOption,
                                inheritedOption,
                                selected,
                                [setting]()
                                {
                                    *setting = static_cast<Value>(0);
                                });
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        for (int index = 0; index < count; ++index)
                        {
                            const int optionValue = index + 1;
                            const bool useInherited =
                                !inheritedOption &&
                                index == inheritedIndex;
                            const bool selected =
                                (!useInherited &&
                                    selectedValue == optionValue) ||
                                (!inheritedOption &&
                                    selectedValue == 0 &&
                                    index == selectedIndex);
                            DrawDeferredDropdownOption(
                                labels[index],
                                labels[index],
                                selected,
                                [setting, optionValue, useInherited]()
                                {
                                    *setting =
                                        static_cast<Value>(
                                            useInherited
                                            ? 0
                                            : optionValue);
                                });
                            if (selected ||
                                (!inheritedOption &&
                                    index == selectedIndex))
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip("%s", tooltip);
                    if (DrawNestedDropdownResetIcon(
                            resetId, value != defaultValue))
                    {
                        value = defaultValue;
                    }
                };

                static constexpr const char* JitterSequenceLabels[] = {
                    "Rotated Grid 4",
                    "Uniform Helix 4",
                    "Halton 8",
                    "Halton 16",
                    "Halton 32",
                    "Sobol 32"
                };
                const int jitterSequence = static_cast<int>(
                    SanitizeTemporalAaJitterSequence(
                        aliasing.temporal.jitterSequence));
                SetNextLabeledControlWidth(
                    "Jitter Sequence##TemporalReconstructive",
                    settingsControlWidth);
                if (BeginRoundedCombo(
                        "Jitter Sequence##TemporalReconstructive",
                        JitterSequenceLabels[jitterSequence]))
                {
                    for (int index = 0;
                        index < static_cast<int>(
                            std::size(JitterSequenceLabels));
                        ++index)
                    {
                        const bool selected = index == jitterSequence;
                        DrawDeferredDropdownOption(
                            JitterSequenceLabels[index],
                            JitterSequenceLabels[index],
                            selected,
                            [settings = &aliasing, index]()
                            {
                                settings->temporal.jitterSequence =
                                    static_cast<TemporalAaJitterSequence>(
                                        index);
                            });
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose camera jitter. Halton 16 is the factory default; "
                    "Sobol 32 uses fixed seed 43 and optimized toroidal "
                    "spacing. Changes reset temporal history.");
                if (DrawNestedDropdownResetIcon(
                        "TemporalJitterSequence",
                        aliasing.temporal.jitterSequence !=
                            aliasingDefaults.temporal.jitterSequence))
                {
                    aliasing.temporal.jitterSequence =
                        aliasingDefaults.temporal.jitterSequence;
                }

                static constexpr const char* DepthValidationLabels[] = {
                    "Stationary Bypass", "Legacy Four-Texel Footprint"
                };
                int depthValidation =
                    aliasing.temporal.nearestTexelDepth ? 0 : 1;
                SetNextLabeledControlWidth(
                    "Depth Validation",
                    settingsControlWidth);
                if (ImGui::Combo(
                        "Depth Validation",
                        &depthValidation,
                        DepthValidationLabels,
                        static_cast<int>(
                            std::size(DepthValidationLabels))))
                {
                    aliasing.temporal.nearestTexelDepth =
                        depthValidation == 0;
                }
                ImGui::SetItemTooltip(
                    "Keep center-owned stationary history phase stable, "
                    "point-validate thin-coverage handoffs, and reject stale "
                    "nearer moving depth; or use the legacy complete "
                    "four-texel footprint.");
                if (DrawNestedDropdownResetIcon(
                        "TemporalDepthValidation",
                        aliasing.temporal.nearestTexelDepth !=
                            aliasingDefaults.temporal.nearestTexelDepth))
                {
                    aliasing.temporal.nearestTexelDepth =
                        aliasingDefaults.temporal.nearestTexelDepth;
                }

                const int presetHistoryFrames = static_cast<int>(
                    GetPresetHistoryFrames(aliasing.temporal.quality));
                int historyFrames =
                    aliasing.temporal.algorithmOverrides.historyFrames < 0
                    ? presetHistoryFrames
                    : aliasing.temporal.algorithmOverrides.historyFrames;
                if (DrawBoundedSliderInt(
                        "History Frames",
                        &historyFrames,
                        1,
                        32,
                        1,
                        16))
                {
                    aliasing.temporal.algorithmOverrides.historyFrames =
                        historyFrames == presetHistoryFrames
                        ? -1
                        : historyFrames;
                }
                ImGui::SetItemTooltip(
                    "Set the visible history horizon. The quality recipe is "
                    "used automatically when this matches its frame count.");
                if (DrawPresetResetIcon(
                        "TemporalHistoryFrames",
                        aliasing.temporal.algorithmOverrides.historyFrames >= 0))
                {
                    aliasing.temporal.algorithmOverrides.historyFrames = -1;
                }

                const float resolvedHistoryStrength =
                    aliasing.temporal.algorithmOverrides.historyStrength < 0.f
                    ? 1.f
                    : aliasing.temporal.algorithmOverrides.historyStrength;
                float historyStrengthPercent =
                    resolvedHistoryStrength * 100.f;
                if (DrawSliderFloat(
                    "History Strength",
                    &historyStrengthPercent,
                    0.f,
                    200.f,
                    "%.0f%%"))
                {
                    const float selectedStrength =
                        historyStrengthPercent * 0.01f;
                    aliasing.temporal.algorithmOverrides.historyStrength =
                        std::abs(selectedStrength - 1.f) < 1e-4f
                        ? -1.f
                        : selectedStrength;
                }
                ImGui::SetItemTooltip(
                    "Scale the contribution of reprojected history. One "
                    "hundred percent follows the quality recipe.");
                if (DrawPresetResetIcon(
                        "TemporalHistoryStrength",
                        aliasing.temporal.algorithmOverrides.historyStrength >=
                            0.f))
                {
                    aliasing.temporal.algorithmOverrides.historyStrength =
                        -1.f;
                }

                ImGui::SetNextItemOpen(false, ImGuiCond_Once);
                if (BeginAnimatedTreeNode(
                        "Cost##TemporalAdvancedCost",
                        ImGuiTreeNodeFlags_None,
                        "Tune retained history cost and output sharpening. "
                        "This section is closed by default."))
                {
                drawPresetEnum(
                    "Mode##TemporalCost",
                    aliasing.temporal.costMode,
                    CostLabels,
                    static_cast<int>(std::size(CostLabels)),
                    temporalCostCustom,
                    applyTemporalCostPreset);
                ImGui::SetItemTooltip(
                    "Choose retained-history quality and cost. Changes append "
                    "(Custom); the arrow restores factory Cost and its controls.");
                if (DrawPresetResetIcon(
                        "TemporalCost",
                        aliasing.temporal.costMode !=
                            aliasingDefaults.temporal.costMode ||
                        temporalCostCustom))
                {
                    applyTemporalCostPreset(
                        aliasingDefaults.temporal.costMode);
                }

                static constexpr const char*
                    StorageLabels[] = { "Robust", "Compact" };
                drawAdvancedEnum(
                    "History Storage",
                    "TemporalHistoryStorage",
                    aliasing.temporal.behaviorOverrides.historyStorage,
                    aliasingDefaults.temporal.behaviorOverrides.historyStorage,
                    static_cast<int>(resolvedAliasing.historyStorage),
                    StorageLabels,
                    static_cast<int>(std::size(StorageLabels)),
                    "Choose robust or compact storage for the retained history.");

                static constexpr const char*
                    WeightLabels[] = {
                        "Confidence Recurrence", "Immediate Horizon"
                    };
                drawAdvancedEnum(
                    "History Weight",
                    "TemporalHistoryWeight",
                    aliasing.temporal.behaviorOverrides.historyWeight,
                    aliasingDefaults.temporal.behaviorOverrides.historyWeight,
                    static_cast<int>(resolvedAliasing.historyWeight),
                    WeightLabels,
                    static_cast<int>(std::size(WeightLabels)),
                    "Choose how confidence changes the retained history weight.");

                static constexpr const char*
                    TrustLabels[] = {
                        "Linear Speed", "Squared Speed"
                    };
                drawAdvancedEnum(
                    "Motion Trust",
                    "TemporalMotionTrust",
                    aliasing.temporal.behaviorOverrides.motionTrust,
                    aliasingDefaults.temporal.behaviorOverrides.motionTrust,
                    static_cast<int>(resolvedAliasing.motionTrust),
                    TrustLabels,
                    static_cast<int>(std::size(TrustLabels)),
                    "Choose how motion speed reduces trust in stored history.");

                static constexpr const char*
                    ClipLabels[] = {
                        "Velocity-Dilated", "Tight Component"
                    };
                drawAdvancedEnum(
                    "Rectification Clip",
                    "TemporalRectificationClip",
                    aliasing.temporal.behaviorOverrides.rectificationClip,
                    aliasingDefaults.temporal.behaviorOverrides.
                        rectificationClip,
                    static_cast<int>(resolvedAliasing.rectificationClip),
                    ClipLabels,
                    static_cast<int>(std::size(ClipLabels)),
                    "Choose the boundary used to clip reprojected history.");

                static constexpr const char*
                    BlendLabels[] = {
                        "Luminance-Compressed", "Linear Color"
                    };
                drawAdvancedEnum(
                    "Blend Domain",
                    "TemporalBlendDomain",
                    aliasing.temporal.behaviorOverrides.blendDomain,
                    aliasingDefaults.temporal.behaviorOverrides.blendDomain,
                    static_cast<int>(resolvedAliasing.blendDomain),
                    BlendLabels,
                    static_cast<int>(std::size(BlendLabels)),
                    "Choose the color domain used to blend current and stored samples.");

                static constexpr const char* SharpenModeLabels[] = {
                    "Off", "On"
                };
                const int automaticSharpeningIndex =
                    resolvedAliasing.sharpeningAllowed ? 1 : 0;
                const std::string automaticSharpening =
                    std::string(SharpenModeLabels[automaticSharpeningIndex]) +
                    " (Automatic)";
                drawAdvancedEnum(
                    "Preset Sharpening",
                    "TemporalPresetSharpening",
                    aliasing.temporal.behaviorOverrides.sharpening,
                    aliasingDefaults.temporal.behaviorOverrides.sharpening,
                    automaticSharpeningIndex,
                    SharpenModeLabels,
                    static_cast<int>(
                        std::size(SharpenModeLabels)),
                    "Choose whether the temporal recipe may sharpen its output.",
                    automaticSharpening.c_str());

                ImGui::Checkbox(
                    "Output Sharpening",
                    &m_ui.TemporalAaSharpenEnabled);
                ImGui::SetItemTooltip(
                    "Apply a final sharpening pass after temporal reconstruction.");
                if (DrawPresetResetIcon(
                        "TemporalOutputSharpening",
                        m_ui.TemporalAaSharpenEnabled))
                {
                    m_ui.TemporalAaSharpenEnabled = false;
                }
                if (BeginAnimatedToggleRegion(
                        "##TemporalSharpenControls",
                        m_ui.TemporalAaSharpenEnabled))
                {
                    DrawSliderFloat(
                        "Sharpness",
                        &m_ui.TemporalAaSharpness,
                        TemporalAaMinimumSharpness,
                        TemporalAaMaximumSharpness,
                        "%.2f");
                    ImGui::SetItemTooltip(
                        "Set the strength of final output sharpening.");
                    if (DrawPresetResetIcon(
                            "TemporalSharpness",
                            m_ui.TemporalAaSharpness !=
                                TemporalAaDefaultSharpness))
                    {
                        m_ui.TemporalAaSharpness =
                            TemporalAaDefaultSharpness;
                    }
                    EndAnimatedToggleRegion();
                }
                EndAnimatedTreeNode();
                }
                EndAnimatedTreeNode();
            }

            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Fast Approximate##Aliasing",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Smooth current-frame edges with Filament-based FXAA."))
            {
            ImGui::Checkbox(
                "Enable##FastApproximate",
                &aliasing.fastApproximate.enabled);
            ImGui::SetItemTooltip(
                "Apply a fast post-tone-map edge filter.");
            if (DrawPresetResetIcon(
                    "FastApproximateEnabled",
                    aliasing.fastApproximate.enabled !=
                        aliasingDefaults.fastApproximate.enabled))
            {
                aliasing.fastApproximate.enabled =
                    aliasingDefaults.fastApproximate.enabled;
            }
            if (BeginAnimatedToggleRegion(
                    "##FastApproximateControls",
                    aliasing.fastApproximate.enabled))
            {
            const bool fastApproximateQualityCustom =
                !MatchesFastApproximateAaQualityPreset(
                    aliasing.fastApproximate);
            const auto applyFastApproximateQualityPreset =
                [settings = &aliasing.fastApproximate](
                    AntiAliasingQuality quality)
                {
                    ApplyFastApproximateAaQualityPreset(
                        *settings, quality);
                };
            drawPresetEnum(
                "Quality##FastApproximate",
                aliasing.fastApproximate.quality,
                QualityLabels,
                static_cast<int>(std::size(QualityLabels)),
                fastApproximateQualityCustom,
                applyFastApproximateQualityPreset);
            ImGui::SetItemTooltip(
                "Choose FXAA Quality. Advanced changes append (Custom); the "
                "arrow restores factory Quality and all FXAA controls.");
            if (DrawPresetResetIcon(
                    "FastApproximateQuality",
                    aliasing.fastApproximate.quality !=
                        aliasingDefaults.fastApproximate.quality ||
                    fastApproximateQualityCustom))
            {
                applyFastApproximateQualityPreset(
                    aliasingDefaults.fastApproximate.quality);
            }

            const FastApproximateAaQualityPreset
                fastApproximatePreset =
                    GetFastApproximateAaQualityPreset(
                        aliasing.fastApproximate.quality);
            ImGui::SetNextItemOpen(false, ImGuiCond_Once);
            if (BeginAnimatedTreeNode(
                    "Advanced##FastApproximate",
                    ImGuiTreeNodeFlags_None,
                    "Tune edge detection and filtering. This section is closed by default."))
            {
                SetNextLabeledControlWidth(
                    "Edge Sharpness##FastApproximate",
                    settingsControlWidth);
                DrawSliderFloat(
                    "Edge Sharpness##FastApproximate",
                    &aliasing.fastApproximate.edgeSharpness,
                    FastApproximateAaMinimumEdgeSharpness,
                    FastApproximateAaMaximumEdgeSharpness,
                    "%.2f");
                ImGui::SetItemTooltip(
                    "Increase to keep the edge filter narrower and sharper.");
                if (DrawNestedDropdownResetIcon(
                        "FastApproximateEdgeSharpness",
                        aliasing.fastApproximate.edgeSharpness !=
                            fastApproximatePreset.edgeSharpness))
                {
                    aliasing.fastApproximate.edgeSharpness =
                        fastApproximatePreset.edgeSharpness;
                }

                SetNextLabeledControlWidth(
                    "Relative Edge Threshold##FastApproximate",
                    settingsControlWidth);
                DrawSliderFloat(
                    "Relative Edge Threshold##FastApproximate",
                    &aliasing.fastApproximate.edgeThreshold,
                    FastApproximateAaMinimumEdgeThreshold,
                    FastApproximateAaMaximumEdgeThreshold,
                    "%.3f");
                ImGui::SetItemTooltip(
                    "Increase to skip more edges relative to local brightness.");
                if (DrawNestedDropdownResetIcon(
                        "FastApproximateEdgeThreshold",
                        aliasing.fastApproximate.edgeThreshold !=
                            fastApproximatePreset.edgeThreshold))
                {
                    aliasing.fastApproximate.edgeThreshold =
                        fastApproximatePreset.edgeThreshold;
                }

                SetNextLabeledControlWidth(
                    "Minimum Edge Threshold##FastApproximate",
                    settingsControlWidth);
                DrawSliderFloat(
                    "Minimum Edge Threshold##FastApproximate",
                    &aliasing.fastApproximate.darkEdgeThreshold,
                    FastApproximateAaMinimumDarkEdgeThreshold,
                    FastApproximateAaMaximumDarkEdgeThreshold,
                    "%.3f");
                ImGui::SetItemTooltip(
                    "Increase to skip more low-contrast edges in dark regions.");
                if (DrawNestedDropdownResetIcon(
                        "FastApproximateMinimumEdgeThreshold",
                        aliasing.fastApproximate.darkEdgeThreshold !=
                            fastApproximatePreset.darkEdgeThreshold))
                {
                    aliasing.fastApproximate.darkEdgeThreshold =
                        fastApproximatePreset.darkEdgeThreshold;
                }
                EndAnimatedTreeNode();
            }
            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Multisample Adaptive##Aliasing",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Render multiple coverage samples for each pixel."))
            {
            ImGui::Checkbox(
                "Enable##MultisampleAdaptive",
                &aliasing.msaa.enabled);
            ImGui::SetItemTooltip(
                "Render multiple geometry coverage samples per pixel.");
            if (DrawPresetResetIcon(
                    "MultisampleEnabled",
                    aliasing.msaa.enabled != aliasingDefaults.msaa.enabled))
            {
                aliasing.msaa.enabled = aliasingDefaults.msaa.enabled;
            }
            if (BeginAnimatedToggleRegion(
                    "##MultisampleAdaptiveControls",
                    aliasing.msaa.enabled))
            {
            const bool multisampleQualityCustom =
                !MatchesMultisampleQualityPreset(aliasing.msaa);
            const auto applyMultisampleQualityPreset =
                [settings = &aliasing.msaa](AntiAliasingQuality quality)
                {
                    ApplyMultisampleQualityPreset(*settings, quality);
                };
            drawPresetEnum(
                "Quality##MultisampleAdaptive",
                aliasing.msaa.quality,
                QualityLabels,
                static_cast<int>(std::size(QualityLabels)),
                multisampleQualityCustom,
                applyMultisampleQualityPreset);
            ImGui::SetItemTooltip(
                "Choose the raster sample-count recipe: 2x, 4x, 8x, or "
                "16x. Advanced changes append (Custom).");
            if (DrawPresetResetIcon(
                    "MultisampleQuality",
                    aliasing.msaa.quality !=
                        aliasingDefaults.msaa.quality ||
                    multisampleQualityCustom))
            {
                applyMultisampleQualityPreset(
                    aliasingDefaults.msaa.quality);
            }

            const uint32_t multisamplePresetSamples =
                GetMultisampleQualitySampleCount(aliasing.msaa.quality);
            ImGui::SetNextItemOpen(false, ImGuiCond_Once);
            if (BeginAnimatedTreeNode(
                    "Advanced##MultisampleAdaptive",
                    ImGuiTreeNodeFlags_None,
                    "Choose the raster sample count and shadow sampling policy. "
                    "This section is closed by default."))
            {
            static constexpr uint32_t SampleCounts[] = {
                2u, 4u, 8u, 16u
            };
            static constexpr const char* SampleLabels[] = {
                "2x", "4x", "8x", "16x"
            };
            int sampleIndex = 1;
            for (int index = 0;
                index < static_cast<int>(std::size(SampleCounts));
                ++index)
            {
                if (aliasing.msaa.sampleCount == SampleCounts[index])
                    sampleIndex = index;
            }
            SetNextLabeledControlWidth(
                "Samples##MultisampleAdaptive",
                settingsControlWidth);
            if (ImGui::Combo(
                    "Samples##MultisampleAdaptive",
                    &sampleIndex,
                    SampleLabels,
                    static_cast<int>(std::size(SampleLabels))))
            {
                aliasing.msaa.sampleCount = SampleCounts[sampleIndex];
            }
            ImGui::SetItemTooltip(
                "Choose the number of geometry coverage samples per pixel.");
            if (DrawNestedDropdownResetIcon(
                    "MultisampleSamples",
                    aliasing.msaa.sampleCount !=
                        multisamplePresetSamples))
            {
                aliasing.msaa.sampleCount =
                    multisamplePresetSamples;
            }
            EndAnimatedTreeNode();
            }
            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            ImGui::PopID();
            EndDrawerBody();
        }
        EndAnimatedToggleRegion();
        }
        const bool debugOpen = DrawCollapsingHeader(
            "Debug",
            "Combine world appearance and effect-specific information views.");
        if (debugOpen)
        {
            BeginDrawerBody("##DebugBody", settingsControlWidth);

            if (BeginAnimatedTreeNode(
                    "World##Debug",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Change material presentation without changing lighting effects."))
            {
            static constexpr const char* WorldLabels[] = {
                "Default",
                "White",
                "White Detail",
                "White Lighting"
            };
            const int worldMode = std::clamp(
                static_cast<int>(m_ui.WhiteWorld),
                0,
                static_cast<int>(std::size(WorldLabels)) - 1);
            SetNextLabeledControlWidth(
                "Materials", settingsControlWidth);
            if (BeginRoundedCombo(
                    "Materials",
                    WorldLabels[worldMode]))
            {
                for (int index = 0;
                    index < static_cast<int>(std::size(WorldLabels));
                    ++index)
                {
                    const WhiteWorldMode candidate =
                        static_cast<WhiteWorldMode>(index);
                    const bool selected = candidate == m_ui.WhiteWorld;
                    DrawDeferredDropdownOption(
                        WorldLabels[index],
                        WorldLabels[index],
                        selected,
                        [this, candidate]()
                        {
                            m_app->SetWhiteWorldMode(candidate);
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose default materials or white world. This combines with "
                "every effect-specific debug view.");
            if (DrawNestedDropdownResetIcon(
                    "DebugWorld",
                    m_ui.WhiteWorld != WhiteWorldMode::Off))
            {
                m_app->SetWhiteWorldMode(WhiteWorldMode::Off);
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedToggleRegion(
                    "##RayMarchingDebugBody",
                    m_ui.Lighting == LightingSolution::RayMarching))
            {
            if (BeginAnimatedTreeNode(
                    "Visibility##Debug",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Inspect composition-stage visibility information."))
            {
            static constexpr const char* VisibilityDebugLabels[] = {
                "Default",
                "Ambient Visibility",
                "Traced Indirect",
                "Applied Indirect"
            };
            const int visibilityDebugView = std::clamp(
                static_cast<int>(m_ui.ScreenSpaceVisibility.debugView),
                0,
                static_cast<int>(std::size(VisibilityDebugLabels)) - 1);
            SetNextLabeledControlWidth(
                "View##VisibilityDebug", settingsControlWidth);
            if (BeginRoundedCombo(
                    "View##VisibilityDebug",
                    VisibilityDebugLabels[visibilityDebugView]))
            {
                for (int index = 0;
                    index < static_cast<int>(
                        std::size(VisibilityDebugLabels));
                    ++index)
                {
                    const VisibilityDebugView candidate =
                        static_cast<VisibilityDebugView>(index);
                    const bool selected =
                        candidate == m_ui.ScreenSpaceVisibility.debugView;
                    DrawDeferredDropdownOption(
                        VisibilityDebugLabels[index],
                        VisibilityDebugLabels[index],
                        selected,
                        [this, candidate]()
                        {
                            m_ui.ScreenSpaceVisibility.debugView = candidate;
                            m_app->ResetImageBasedLightingHistory();
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Show the composite, ambient visibility, traced indirect light, "
                "or indirect response after material application.");
            if (DrawNestedDropdownResetIcon(
                    "DebugVisibility",
                    m_ui.ScreenSpaceVisibility.debugView !=
                        VisibilityDebugView::FinalImage))
            {
                m_ui.ScreenSpaceVisibility.debugView =
                    VisibilityDebugView::FinalImage;
                m_app->ResetImageBasedLightingHistory();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Physically Based Lighting##Debug",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Inspect material and environment-lighting information."))
            {
            static constexpr const char* LightingLabels[] = {
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
                "Environment Level",
                "Sky Visibility"
            };
            const int lightingView = std::clamp(
                static_cast<int>(m_ui.LightingDebugView),
                0,
                static_cast<int>(std::size(LightingLabels)) - 1);
            const bool skyVisibilityDebugAvailable =
                m_ui.RayTracedSkyVisibility.enabled &&
                m_ui.Representation.allowRayTraversal &&
                m_app->SupportsRayTracedSkyVisibility();
            SetNextLabeledControlWidth(
                "Information Filter", settingsControlWidth);
            if (BeginRoundedCombo(
                    "Information Filter",
                    LightingLabels[lightingView]))
            {
                for (int index = 0;
                    index < static_cast<int>(std::size(LightingLabels));
                    ++index)
                {
                    const PbrLightingDebugView candidate =
                        static_cast<PbrLightingDebugView>(index);
                    const bool selected =
                        candidate == m_ui.LightingDebugView;
                    const bool available =
                        candidate != PbrLightingDebugView::SkyVisibility ||
                        skyVisibilityDebugAvailable;
                    if (!available)
                        ImGui::BeginDisabled();
                    DrawDeferredDropdownOption(
                        LightingLabels[index],
                        LightingLabels[index],
                        selected,
                        [this, candidate]()
                        {
                            m_ui.LightingDebugView = candidate;
                            m_app->ResetImageBasedLightingHistory();
                        });
                    if (!available)
                    {
                        ImGui::SetItemTooltip(
                            "Enable supported ray traversal and Ray Traced Sky "
                            "Visibility to inspect this signal.");
                        ImGui::EndDisabled();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose which material or environment-lighting quantity is "
                "shown. Visibility remains enabled and can still be inspected.");
            if (DrawNestedDropdownResetIcon(
                    "DebugLighting",
                    m_ui.LightingDebugView != PbrLightingDebugView::None))
            {
                m_ui.LightingDebugView = PbrLightingDebugView::None;
                m_app->ResetImageBasedLightingHistory();
            }
            EndAnimatedTreeNode();
            }

            EndAnimatedToggleRegion();
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
            const ImageBasedLightingSourceInfo&
                selectedEnvironmentInfo =
                    GetImageBasedLightingSourceInfo(
                        m_ui.EnvironmentSource);
            SetNextLabeledControlWidth(
                "Environment##SkyEnvironment",
                settingsControlWidth);
            if (BeginRoundedCombo(
                    "Environment##SkyEnvironment",
                    selectedEnvironmentInfo.displayName))
            {
                for (uint32_t index = 0u;
                    index < uint32_t(ImageBasedLightingSource::Count);
                    ++index)
                {
                    const ImageBasedLightingSource source =
                        ImageBasedLightingSource(index);
                    const ImageBasedLightingSourceInfo& info =
                        GetImageBasedLightingSourceInfo(source);
                    const bool selected =
                        source == m_ui.EnvironmentSource;
                    DrawDeferredDropdownOption(
                        info.displayName,
                        info.displayName,
                        selected,
                        [this, source]()
                        {
                            const ImageBasedLightingSourceInfo& selectedInfo =
                                GetImageBasedLightingSourceInfo(source);
                            const bool presentationChanged =
                                m_ui.EnvironmentSource != source ||
                                m_ui.EnvironmentExposureStops !=
                                    selectedInfo.defaultExposureStops;
                            m_ui.EnvironmentSource = source;
                            m_ui.EnvironmentExposureStops =
                                selectedInfo.defaultExposureStops;
                            if (presentationChanged)
                            {
                                m_app->ResetImageBasedLightingHistory();
                            }
                        });
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the imported radiance source used by image-based "
                "lighting and the "
                "optional matching background.");
            constexpr ImageBasedLightingSource DefaultEnvironmentSource =
                ImageBasedLightingSource::Kloppenheim03Day;
            if (DrawPresetResetIcon(
                    "Environment Source",
                    m_ui.EnvironmentSource != DefaultEnvironmentSource))
            {
                QueueDeferredControlUiAction(
                    [this]()
                    {
                        constexpr ImageBasedLightingSource
                            DefaultSource =
                                ImageBasedLightingSource::Kloppenheim03Day;
                        m_ui.EnvironmentSource = DefaultSource;
                        m_ui.EnvironmentExposureStops =
                            GetImageBasedLightingSourceInfo(
                                DefaultSource).defaultExposureStops;
                        m_app->ResetImageBasedLightingHistory();
                    });
            }

            const float defaultEnvironmentExposure =
                GetImageBasedLightingSourceInfo(
                    m_ui.EnvironmentSource).defaultExposureStops;
            if (DrawSliderFloat(
                    "Exposure##ImageBasedLighting",
                    &m_ui.EnvironmentExposureStops,
                    -8.f,
                    8.f,
                    "%+.2f stops"))
            {
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Scale lighting and the matching background together.");
            if (DrawPresetResetIcon(
                    "Environment Exposure",
                    m_ui.EnvironmentExposureStops !=
                        defaultEnvironmentExposure))
            {
                m_ui.EnvironmentExposureStops =
                    defaultEnvironmentExposure;
                m_app->ResetImageBasedLightingHistory();
            }

            if (ImGui::Checkbox(
                    "Show Environment Background",
                    &m_ui.ShowEnvironmentBackground))
            {
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Show the same environment used for lighting.");
            if (DrawPresetResetIcon(
                    "Environment Background Enabled",
                    !m_ui.ShowEnvironmentBackground))
            {
                m_ui.ShowEnvironmentBackground = true;
                m_app->ResetImageBasedLightingHistory();
            }

            if (BeginAnimatedTreeNode(
                    "Auto Exposure##Sky",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Adapt display exposure without changing the established "
                    "tonemapper or physical lighting."))
            {
                if (ImGui::Checkbox(
                        "Enable##AutoExposure",
                        &m_ui.AutoExposure.enabled))
                {
                    m_ui.AutoExposure =
                        SanitizeAutoExposureSettings(m_ui.AutoExposure);
                }
                ImGui::SetItemTooltip(
                    "Adapt display exposure to the median scene luminance. "
                    "Lighting, ray effects, and their histories are unchanged.");
                if (DrawPresetResetIcon(
                        "Auto Exposure Enabled",
                        m_ui.AutoExposure.enabled))
                {
                    m_ui.AutoExposure.enabled = false;
                }
                if (BeginAnimatedToggleRegion(
                        "##AutoExposureControls",
                        m_ui.AutoExposure.enabled))
                {
                    if (DrawBoundedSliderFloat(
                            "Exposure Compensation",
                            &m_ui.AutoExposure.exposureCompensationEV,
                            AutoExposureMinimumCompensationEV,
                            AutoExposureMaximumCompensationEV,
                            -8.f,
                            8.f,
                            "%+.2f EV"))
                    {
                        m_ui.AutoExposure =
                            SanitizeAutoExposureSettings(m_ui.AutoExposure);
                    }
                    ImGui::SetItemTooltip(
                        "Bias the bounded automatic exposure result in "
                        "exposure-value stops.");
                    if (DrawPresetResetIcon(
                            "Auto Exposure Compensation",
                            m_ui.AutoExposure.exposureCompensationEV !=
                                AutoExposureDefaultCompensationEV))
                    {
                        m_ui.AutoExposure.exposureCompensationEV =
                            AutoExposureDefaultCompensationEV;
                    }
                    if (DrawBoundedSliderFloat(
                            "Maximum Brightening",
                            &m_ui.AutoExposure.maximumBrighteningEV,
                            AutoExposureMinimumMovementEV,
                            AutoExposureMaximumMovementEV,
                            AutoExposureMinimumMovementEV,
                            8.f,
                            "%.2f EV"))
                    {
                        m_ui.AutoExposure =
                            SanitizeAutoExposureSettings(m_ui.AutoExposure);
                    }
                    ImGui::SetItemTooltip(
                        "Limit how far automatic metering may raise exposure. "
                        "Exposure Compensation is applied afterward.");
                    if (DrawPresetResetIcon(
                            "Auto Exposure Maximum Brightening",
                            m_ui.AutoExposure.maximumBrighteningEV !=
                                AutoExposureDefaultMaximumBrighteningEV))
                    {
                        m_ui.AutoExposure.maximumBrighteningEV =
                            AutoExposureDefaultMaximumBrighteningEV;
                    }
                    if (DrawBoundedSliderFloat(
                            "Maximum Darkening",
                            &m_ui.AutoExposure.maximumDarkeningEV,
                            AutoExposureMinimumMovementEV,
                            AutoExposureMaximumMovementEV,
                            AutoExposureMinimumMovementEV,
                            8.f,
                            "%.2f EV"))
                    {
                        m_ui.AutoExposure =
                            SanitizeAutoExposureSettings(m_ui.AutoExposure);
                    }
                    ImGui::SetItemTooltip(
                        "Limit how far automatic metering may lower exposure. "
                        "Exposure Compensation is applied afterward.");
                    if (DrawPresetResetIcon(
                            "Auto Exposure Maximum Darkening",
                            m_ui.AutoExposure.maximumDarkeningEV !=
                                AutoExposureDefaultMaximumDarkeningEV))
                    {
                        m_ui.AutoExposure.maximumDarkeningEV =
                            AutoExposureDefaultMaximumDarkeningEV;
                    }
                    if (DrawSliderFloat(
                            "Adjustment Period",
                            &m_ui.AutoExposure.adjustmentPeriodSeconds,
                            AutoExposureMinimumAdjustmentPeriodSeconds,
                            AutoExposureMaximumAdjustmentPeriodSeconds,
                            "%.2f s"))
                    {
                        m_ui.AutoExposure =
                            SanitizeAutoExposureSettings(m_ui.AutoExposure);
                    }
                    ImGui::SetItemTooltip(
                        "Set the half-life of exposure adaptation. This affects "
                        "only display adaptation, not lighting or effect histories.");
                    if (DrawPresetResetIcon(
                            "Auto Exposure Adjustment Period",
                            m_ui.AutoExposure.adjustmentPeriodSeconds !=
                                AutoExposureDefaultAdjustmentPeriodSeconds))
                    {
                        m_ui.AutoExposure.adjustmentPeriodSeconds =
                            AutoExposureDefaultAdjustmentPeriodSeconds;
                    }
                    EndAnimatedToggleRegion();
                }
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedToggleRegion(
                    "##RayMarchingAmbientFill",
                    m_ui.Lighting == LightingSolution::RayMarching))
            {
            if (ImGui::Checkbox(
                    "Ambient Fill",
                    &m_ui.EnableAmbientFill))
            {
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Enable diffuse/specular environment fill. Disable it to "
                "isolate direct lights; settings persist, and Occlusion "
                "needs it.");
            if (DrawPresetResetIcon(
                    "Ambient Fill Enabled",
                    !m_ui.EnableAmbientFill))
            {
                m_ui.EnableAmbientFill = true;
                m_app->ResetImageBasedLightingHistory();
            }
            if (BeginAnimatedToggleRegion(
                    "##AmbientFillControls",
                    m_ui.EnableAmbientFill))
            {
                if (ImGui::Checkbox(
                        "Diffuse Environment",
                        &m_ui.EnableDiffuseIbl))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Use the selected environment for diffuse lighting.");
                if (DrawPresetResetIcon(
                        "Diffuse Environment Enabled",
                        !m_ui.EnableDiffuseIbl))
                {
                    m_ui.EnableDiffuseIbl = true;
                    m_app->ResetImageBasedLightingHistory();
                }
                if (BeginAnimatedToggleRegion(
                        "##DiffuseIblControls",
                        m_ui.EnableDiffuseIbl))
                {
                    if (DrawBoundedSliderFloat(
                            "Diffuse Strength##ImageBasedLighting",
                            &m_ui.DiffuseIblStrength,
                            0.f,
                            4.f,
                            0.f,
                            2.f,
                            "%.2f"))
                    {
                        m_app->ResetImageBasedLightingHistory();
                    }
                    ImGui::SetItemTooltip(
                        "Scale diffuse environment lighting after exposure.");
                    if (DrawPresetResetIcon(
                            "Diffuse Environment Strength",
                            m_ui.DiffuseIblStrength != 1.f))
                    {
                        m_ui.DiffuseIblStrength = 1.f;
                        m_app->ResetImageBasedLightingHistory();
                    }
                    EndAnimatedToggleRegion();
                }

                if (ImGui::Checkbox(
                        "Specular Environment",
                        &m_ui.EnableSpecularIbl))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Use the selected environment for specular reflections.");
                if (DrawPresetResetIcon(
                        "Specular Environment Enabled",
                        !m_ui.EnableSpecularIbl))
                {
                    m_ui.EnableSpecularIbl = true;
                    m_app->ResetImageBasedLightingHistory();
                }
                if (BeginAnimatedToggleRegion(
                        "##SpecularIblControls",
                        m_ui.EnableSpecularIbl))
                {
                    if (DrawBoundedSliderFloat(
                            "Specular Strength##ImageBasedLighting",
                            &m_ui.SpecularIblStrength,
                            0.f,
                            4.f,
                            0.f,
                            2.f,
                            "%.2f"))
                    {
                        m_app->ResetImageBasedLightingHistory();
                    }
                    ImGui::SetItemTooltip(
                        "Scale specular environment lighting after exposure.");
                    if (DrawPresetResetIcon(
                            "Specular Environment Strength",
                            m_ui.SpecularIblStrength != 1.f))
                    {
                        m_ui.SpecularIblStrength = 1.f;
                        m_app->ResetImageBasedLightingHistory();
                    }
                    EndAnimatedToggleRegion();
                }

                EndAnimatedToggleRegion();
            }
            EndAnimatedToggleRegion();
            }

            if (BeginAnimatedToggleRegion(
                    "##RayMarchingSkyVisibility",
                    m_ui.Lighting == LightingSolution::RayMarching))
            {
            ImGui::Spacing();
            if (BeginAnimatedTreeNode(
                    "Ray Traced Sky Visibility##Sky",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure ray traced environment visibility. This effect "
                    "section remains independently collapsible while enabled."))
            {
            RayTracedSkyVisibilitySettings& skyVisibility =
                m_ui.RayTracedSkyVisibility;
            const RayTracedSkyVisibilitySettings skyVisibilityDefaults{};
            const bool skyVisibilityAvailable =
                m_app->SupportsRayTracedSkyVisibility();
            const bool disableSkyVisibilityEnable =
                !skyVisibilityAvailable && !skyVisibility.enabled;
            if (disableSkyVisibilityEnable)
                ImGui::BeginDisabled();
            if (ImGui::Checkbox(
                    "Enable##RayTracedSkyVisibility",
                    &skyVisibility.enabled))
            {
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Trace current frame world space visibility for the selected "
                "diffuse and specular environment lighting consumers.");
            if (disableSkyVisibilityEnable)
                ImGui::EndDisabled();
            if (DrawPresetResetIcon(
                    "RayTracedSkyVisibilityEnabled",
                    skyVisibility.enabled != skyVisibilityDefaults.enabled))
            {
                skyVisibility.enabled = skyVisibilityDefaults.enabled;
                m_app->ResetImageBasedLightingHistory();
            }

            if (BeginAnimatedToggleRegion(
                    "##RayTracedSkyVisibilityControls",
                    skyVisibility.enabled && skyVisibilityAvailable))
            {
                if (ImGui::Checkbox(
                        "Effect Diffuse##RayTracedSkyVisibility",
                        &skyVisibility.applyToDiffuseIbl))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Apply the scalar visibility to diffuse environment "
                    "lighting before final composition and GI source "
                    "radiance.");
                if (DrawPresetResetIcon(
                        "RayTracedSkyVisibilityDiffuseIbl",
                        skyVisibility.applyToDiffuseIbl !=
                            skyVisibilityDefaults.applyToDiffuseIbl))
                {
                    skyVisibility.applyToDiffuseIbl =
                        skyVisibilityDefaults.applyToDiffuseIbl;
                    m_app->ResetImageBasedLightingHistory();
                }

                if (ImGui::Checkbox(
                        "Effect Specular##RayTracedSkyVisibility",
                        &skyVisibility.applyToSpecularIbl))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Apply cosine-weighted normal-hemisphere visibility to "
                    "specular lighting; it ignores reflection direction and "
                    "roughness.");
                if (DrawPresetResetIcon(
                        "RayTracedSkyVisibilitySpecularIbl",
                        skyVisibility.applyToSpecularIbl !=
                            skyVisibilityDefaults.applyToSpecularIbl))
                {
                    skyVisibility.applyToSpecularIbl =
                        skyVisibilityDefaults.applyToSpecularIbl;
                    m_app->ResetImageBasedLightingHistory();
                }

                if (ImGui::Checkbox(
                        "Output Hit Distance##RayTracedSkyVisibility",
                        &skyVisibility.outputHitDistance))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Output the physical closest blocker distance for sky-"
                    "visibility denoising. Disabled adds no hit-distance cost.");
                if (DrawPresetResetIcon(
                        "RayTracedSkyVisibilityOutputHitDistance",
                        skyVisibility.outputHitDistance !=
                            skyVisibilityDefaults.outputHitDistance))
                {
                    skyVisibility.outputHitDistance =
                        skyVisibilityDefaults.outputHitDistance;
                    m_app->ResetImageBasedLightingHistory();
                }

                int sampleRateLog2 = skyVisibility.sampleRateLog2;
                int sampleRate = 1 << sampleRateLog2;
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (DrawSliderInt(
                        "Samples Per Pixel##RayTracedSkyVisibility",
                        &sampleRate,
                        1,
                        1 << RayTracedSkyVisibilityMaximumSampleRateLog2,
                        "%d",
                        ImGuiSliderFlags_AlwaysClamp |
                            ImGuiSliderFlags_Logarithmic))
                {
                    const int candidateSampleRateLog2 = std::clamp(
                        static_cast<int>(std::lround(std::log2(
                            static_cast<double>(
                                std::max(1, sampleRate))))),
                        RayTracedSkyVisibilityMinimumSampleRateLog2,
                        RayTracedSkyVisibilityMaximumSampleRateLog2);
                    if (candidateSampleRateLog2 !=
                        skyVisibility.sampleRateLog2)
                    {
                        skyVisibility.sampleRateLog2 =
                            candidateSampleRateLog2;
                        m_app->ResetImageBasedLightingHistory();
                    }
                }
                ImGui::SetItemTooltip(
                    "Trace and average 1 to 64 cosine-weighted normal-hemisphere "
                    "rays per pixel.");
                if (DrawPresetResetIcon(
                        "RayTracedSkyVisibilitySamples",
                        skyVisibility.sampleRateLog2 !=
                            skyVisibilityDefaults.sampleRateLog2))
                {
                    skyVisibility.sampleRateLog2 =
                        skyVisibilityDefaults.sampleRateLog2;
                    m_app->ResetImageBasedLightingHistory();
                }

                const NoiseSettings oldResolvedNoise = ResolveNoiseSettings(
                    m_ui.Noise,
                    skyVisibility.noise);
                bool noiseOverrideChanged = false;
                if (ImGui::Checkbox(
                        "Specify Noise##RayTracedSkyVisibility",
                        &skyVisibility.noise.specifyNoise))
                {
                    noiseOverrideChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Use custom noise sampling for this effect only. This "
                    "does not change the noise sampling used by any other "
                    "effect.");
                if (DrawPresetResetIcon(
                        "RayTracedSkyVisibilitySpecifyNoise",
                        skyVisibility.noise.specifyNoise !=
                            skyVisibilityDefaults.noise.specifyNoise))
                {
                    skyVisibility.noise.specifyNoise =
                        skyVisibilityDefaults.noise.specifyNoise;
                    noiseOverrideChanged = true;
                }
                if (BeginAnimatedToggleRegion(
                        "##RayTracedSkyVisibilityCustomNoise",
                        skyVisibility.noise.specifyNoise))
                {
                    noiseOverrideChanged |= drawNoiseSettingsControls(
                        skyVisibility.noise.custom,
                        skyVisibilityDefaults.noise.custom,
                        "RayTracedSkyVisibility",
                        true);
                    EndAnimatedToggleRegion();
                }
                const NoiseSettings newResolvedNoise = ResolveNoiseSettings(
                    m_ui.Noise,
                    skyVisibility.noise);
                if (noiseOverrideChanged &&
                    oldResolvedNoise != newResolvedNoise)
                {
                    m_app->ResetNoiseSamplingHistory(
                        false, false, true, false);
                }

                int maxDistance =
                    static_cast<int>(skyVisibility.maxDistance);
                SetNextLabeledControlWidth(
                    "Max Distance##RayTracedSkyVisibility",
                    settingsControlWidth);
                if (ImGui::Combo(
                        "Max Distance##RayTracedSkyVisibility",
                        &maxDistance,
                        RayVisibilityMaxDistanceLabels.data(),
                        static_cast<int>(
                            RayVisibilityMaxDistanceLabels.size())))
                {
                    skyVisibility.maxDistance =
                        static_cast<RayVisibilityMaxDistance>(
                            maxDistance);
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Max uses the scene diagonal. Finite distances ignore "
                    "farther blockers and provide bounded, not exact, sky "
                    "visibility.");
                if (DrawPresetResetIcon(
                        "RayTracedSkyVisibilityMaxDistance",
                        skyVisibility.maxDistance !=
                            skyVisibilityDefaults.maxDistance))
                {
                    skyVisibility.maxDistance =
                        skyVisibilityDefaults.maxDistance;
                    m_app->ResetImageBasedLightingHistory();
                }

                if (DrawSliderFloat(
                        "Ray Bias##RayTracedSkyVisibility",
                        &skyVisibility.rayBias,
                        0.f,
                        RayTracedSkyVisibilityMaximumRayBias,
                        "%.4f"))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Offset the ray origin along the view-facing raster-"
                    "triangle normal; large values can detach contact occlusion.");
                if (DrawPresetResetIcon(
                        "RayTracedSkyVisibilityRayBias",
                        skyVisibility.rayBias !=
                            skyVisibilityDefaults.rayBias))
                {
                    skyVisibility.rayBias =
                        skyVisibilityDefaults.rayBias;
                    m_app->ResetImageBasedLightingHistory();
                }

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
                EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }
            EndAnimatedToggleRegion();
            }

            EndDrawerBody();
        }
        ImGui::Spacing();

        const auto& lights = m_app->GetEditableLights();
        std::shared_ptr<Light> defaultSelectedLight =
            m_app->GetPrimaryDirectionalLight();
        if (!defaultSelectedLight ||
            std::find(
                lights.begin(),
                lights.end(),
                defaultSelectedLight) == lights.end())
        {
            defaultSelectedLight =
                lights.empty() ? nullptr : lights.front();
        }
        if (lights.empty())
        {
            m_SelectedLight.reset();
        }
        else if (std::find(lights.begin(), lights.end(), m_SelectedLight) == lights.end())
        {
            m_SelectedLight = defaultSelectedLight;
        }

        const bool lightsOpen = DrawCollapsingHeader(
            "Lights", "Show scene light controls.");
        if (lightsOpen)
        {
            BeginDrawerBody(
                "##LightsBody",
                settingsControlWidth);
            if (!lights.empty())
            {
                ImGui::SetNextItemWidth(settingsControlWidth);
                const bool lightComboOpen = BeginRoundedCombo(
                    "Select Light", m_SelectedLight ? m_SelectedLight->GetName().c_str() : "(None)");
                ImGui::SetItemTooltip("Choose a light to edit.");
                if (lightComboOpen)
                {
                    for (const auto& light : lights)
                    {
                        const bool selected = m_SelectedLight == light;
                        DrawDeferredDropdownOption(
                            light->GetName().c_str(),
                            light->GetName().c_str(),
                            selected,
                            [this, light]()
                            {
                                m_SelectedLight = light;
                            });
                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (DrawPresetResetIcon(
                        "Selected Light",
                        m_SelectedLight != defaultSelectedLight,
                        "Select the scene's primary directional light."))
                {
                    m_SelectedLight = defaultSelectedLight;
                }

                if (m_SelectedLight)
                {
                    if (m_app->IsFlashlight(m_SelectedLight))
                    {
                        FlashlightSettings& flashlight =
                            m_ui.Flashlight;
                        const FlashlightSettings flashlightBeforeControls =
                            flashlight;
                        const FlashlightSettings defaults =
                            DefaultFlashlightSettings;
                        const auto floatChanged =
                            [](float left, float right)
                            {
                                return std::abs(left - right) > 1e-5f;
                            };

                        if (ImGui::Checkbox(
                                "Enabled",
                                &m_ui.FlashlightEnabled))
                        {
                            m_app->ResetImageBasedLightingHistory();
                        }
                        ImGui::SetItemTooltip(
                            "Turn the camera flashlight on or off. Plain F "
                            "uses the same setting.");
                        if (DrawPresetResetIcon(
                                "Flashlight Enabled",
                                m_ui.FlashlightEnabled !=
                                    DefaultFlashlightEnabled))
                        {
                            m_ui.FlashlightEnabled =
                                DefaultFlashlightEnabled;
                            m_app->ResetImageBasedLightingHistory();
                        }
                        if (BeginAnimatedToggleRegion(
                                "##RayMarchingFlashlightVisibility",
                                m_ui.Lighting ==
                                    LightingSolution::RayMarching))
                        {
                        ImGui::Checkbox(
                            "Cast Shadows",
                            &flashlight.castShadows);
                        ImGui::SetItemTooltip(
                            "Trace flashlight visibility through the shared "
                            "scene representation.");
                        if (DrawPresetResetIcon(
                                "Flashlight Cast Shadows",
                                flashlight.castShadows !=
                                    defaults.castShadows))
                        {
                            flashlight.castShadows =
                                defaults.castShadows;
                        }
                        if (BeginAnimatedToggleRegion(
                                "##FlashlightShadowControls",
                                flashlight.castShadows))
                        {
                            ImGui::Checkbox(
                                "Output Hit Distance##Flashlight",
                                &flashlight.outputHitDistance);
                            ImGui::SetItemTooltip(
                                "Output the physical closest blocker distance "
                                "used by shadow denoising. This adds a ray "
                                "output only when enabled.");
                            if (DrawPresetResetIcon(
                                    "Flashlight Output Hit Distance",
                                    flashlight.outputHitDistance !=
                                        defaults.outputHitDistance))
                            {
                                flashlight.outputHitDistance =
                                    defaults.outputHitDistance;
                            }
                            EndAnimatedToggleRegion();
                        }
                        EndAnimatedToggleRegion();
                        }

                        ImGui::Checkbox(
                            "Realistic Flashlight",
                            &flashlight.realisticLens);
                        ImGui::SetItemTooltip(
                            "Add a lens hotspot, bounded sway, and aim "
                            "correction inside one physical spot light.");
                        if (DrawPresetResetIcon(
                                "Realistic Flashlight",
                                flashlight.realisticLens !=
                                    defaults.realisticLens))
                        {
                            flashlight.realisticLens =
                                defaults.realisticLens;
                        }

                        if (BeginAnimatedToggleRegion(
                                "##RealisticFlashlightControls",
                                flashlight.realisticLens))
                        {
                            DrawSliderFloat(
                                "Hotspot Size",
                                &flashlight.hotspotSize,
                                FlashlightMinimumHotspotSize,
                                FlashlightMaximumHotspotSize,
                                "%.2f");
                            ImGui::SetItemTooltip(
                                "Set the focused lens hotspot width relative "
                                "to the complete beam.");
                            if (DrawPresetResetIcon(
                                    "Flashlight Hotspot Size",
                                    floatChanged(
                                        flashlight.hotspotSize,
                                        defaults.hotspotSize)))
                            {
                                flashlight.hotspotSize =
                                    defaults.hotspotSize;
                            }

                            DrawSliderFloat(
                                "Hotspot Strength",
                                &flashlight.hotspotStrength,
                                0.f,
                                FlashlightMaximumHotspotStrength,
                                "%.2f");
                            ImGui::SetItemTooltip(
                                "Move peak candela from the broad spill into "
                                "the focused lens hotspot.");
                            if (DrawPresetResetIcon(
                                    "Flashlight Hotspot Strength",
                                    floatChanged(
                                        flashlight.hotspotStrength,
                                        defaults.hotspotStrength)))
                            {
                                flashlight.hotspotStrength =
                                    defaults.hotspotStrength;
                            }

                            ImGui::Checkbox(
                                "Stationary When Idle",
                                &flashlight.stationaryWhenIdle);
                            static constexpr char
                                FlashlightStationaryWhenIdleTooltip[] =
                                    "Freeze the flashlight pose when the active "
                                    "camera rests. Camera motion or motion-setting "
                                    "changes resume it.";
                            static_assert(
                                sizeof(FlashlightStationaryWhenIdleTooltip) -
                                    1u <=
                                120u);
                            ImGui::SetItemTooltip(
                                "%s",
                                FlashlightStationaryWhenIdleTooltip);
                            if (DrawPresetResetIcon(
                                    "Flashlight Stationary When Idle",
                                    flashlight.stationaryWhenIdle !=
                                        defaults.stationaryWhenIdle))
                            {
                                flashlight.stationaryWhenIdle =
                                    defaults.stationaryWhenIdle;
                            }

                            DrawBoundedSliderFloat(
                                "Sway",
                                &flashlight.swayDegrees,
                                0.f,
                                FlashlightMaximumSwayDegrees,
                                0.f,
                                1.f,
                                "%.2f degrees");
                            ImGui::SetItemTooltip(
                                "Set the maximum subtle handheld aim motion. "
                                "Zero keeps the corrected beam perfectly still.");
                            if (DrawPresetResetIcon(
                                    "Flashlight Sway",
                                    floatChanged(
                                        flashlight.swayDegrees,
                                        defaults.swayDegrees)))
                            {
                                flashlight.swayDegrees =
                                    defaults.swayDegrees;
                            }

                            DrawSliderFloat(
                                "Aim Correction",
                                &flashlight.aimCorrectionSeconds,
                                FlashlightMinimumAimCorrectionSeconds,
                                FlashlightMaximumAimCorrectionSeconds,
                                "%.2f s");
                            ImGui::SetItemTooltip(
                                "Set the half-life for the beam to catch up "
                                "after the camera turns.");
                            if (DrawPresetResetIcon(
                                    "Flashlight Aim Correction",
                                    floatChanged(
                                        flashlight.aimCorrectionSeconds,
                                        defaults.aimCorrectionSeconds)))
                            {
                                flashlight.aimCorrectionSeconds =
                                    defaults.aimCorrectionSeconds;
                            }

                            EndAnimatedToggleRegion();
                        }

                        DrawSliderFloat(
                            "Brightness",
                            &flashlight.peakIntensityCandela,
                            FlashlightMinimumIntensityCandela,
                            FlashlightMaximumIntensityCandela,
                            "%.0f candela",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the peak on-axis luminous intensity.");
                        if (DrawPresetResetIcon(
                                "Flashlight Brightness",
                                floatChanged(
                                    flashlight.peakIntensityCandela,
                                    defaults.peakIntensityCandela)))
                        {
                            flashlight.peakIntensityCandela =
                                defaults.peakIntensityCandela;
                        }

                        DrawBoundedSliderFloat(
                            "Beam Size",
                            &flashlight.beamSizeDegrees,
                            FlashlightMinimumBeamSizeDegrees,
                            FlashlightMaximumBeamSizeDegrees,
                            FlashlightMinimumBeamSizeDegrees,
                            60.f,
                            "%.1f degrees");
                        ImGui::SetItemTooltip(
                            "Set the full horizontal and vertical outer beam "
                            "width.");
                        if (DrawPresetResetIcon(
                                "Flashlight Beam Size",
                                floatChanged(
                                    flashlight.beamSizeDegrees,
                                    defaults.beamSizeDegrees)))
                        {
                            flashlight.beamSizeDegrees =
                                defaults.beamSizeDegrees;
                        }

                        DrawBoundedSliderFloat(
                            "Angular Size",
                            &flashlight.angularSizeDegrees,
                            FlashlightMinimumAngularSizeDegrees,
                            FlashlightMaximumAngularSizeDegrees,
                            FlashlightMinimumAngularSizeDegrees,
                            10.f,
                            "%.2f degrees");
                        ImGui::SetItemTooltip(
                            "Set the apparent diameter of the analytical "
                            "spherical emitter at one metre; apparent size "
                            "varies with surface distance.");
                        if (DrawPresetResetIcon(
                                "Flashlight Angular Size",
                                floatChanged(
                                    flashlight.angularSizeDegrees,
                                    defaults.angularSizeDegrees)))
                        {
                            flashlight.angularSizeDegrees =
                                defaults.angularSizeDegrees;
                        }

                        DrawSliderFloat(
                            "Beam Roundness",
                            &flashlight.beamRoundness,
                            0.f,
                            1.f,
                            "%.2f");
                        ImGui::SetItemTooltip(
                            "Morph the beam footprint from a softly rounded "
                            "square to an exact circle.");
                        if (DrawPresetResetIcon(
                                "Flashlight Beam Roundness",
                                floatChanged(
                                    flashlight.beamRoundness,
                                    defaults.beamRoundness)))
                        {
                            flashlight.beamRoundness =
                                defaults.beamRoundness;
                        }

                        DrawSliderFloat(
                            "Edge Softness",
                            &flashlight.edgeSoftness,
                            0.f,
                            1.f,
                            "%.2f");
                        ImGui::SetItemTooltip(
                            "Set the falloff width without changing the "
                            "projected beam shape.");
                        if (DrawPresetResetIcon(
                                "Flashlight Edge Softness",
                                floatChanged(
                                    flashlight.edgeSoftness,
                                    defaults.edgeSoftness)))
                        {
                            flashlight.edgeSoftness =
                                defaults.edgeSoftness;
                        }

                        DrawSliderFloat(
                            "Range",
                            &flashlight.rangeMeters,
                            FlashlightMinimumRangeMeters,
                            FlashlightMaximumRangeMeters,
                            "%.1f m",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the finite distance where the beam fades out.");
                        if (DrawPresetResetIcon(
                                "Flashlight Range",
                                floatChanged(
                                    flashlight.rangeMeters,
                                    defaults.rangeMeters)))
                        {
                            flashlight.rangeMeters =
                                defaults.rangeMeters;
                        }

                        float flashlightColor[] = {
                            flashlight.colorLinearRed,
                            flashlight.colorLinearGreen,
                            flashlight.colorLinearBlue
                        };
                        if (DrawUvsrColorEdit(
                                "Color",
                                flashlightColor,
                                UvsrColorEditChannels::Rgb))
                        {
                            flashlight.colorLinearRed =
                                flashlightColor[0];
                            flashlight.colorLinearGreen =
                                flashlightColor[1];
                            flashlight.colorLinearBlue =
                                flashlightColor[2];
                        }
                        ImGui::SetItemTooltip(
                            "Set the flashlight's scene-linear red, green, and "
                            "blue color.");
                        if (DrawPresetResetIcon(
                                "Flashlight Color",
                                floatChanged(
                                    flashlight.colorLinearRed,
                                    defaults.colorLinearRed) ||
                                floatChanged(
                                    flashlight.colorLinearGreen,
                                    defaults.colorLinearGreen) ||
                                floatChanged(
                                    flashlight.colorLinearBlue,
                                    defaults.colorLinearBlue)))
                        {
                            flashlight.colorLinearRed =
                                defaults.colorLinearRed;
                            flashlight.colorLinearGreen =
                                defaults.colorLinearGreen;
                            flashlight.colorLinearBlue =
                                defaults.colorLinearBlue;
                        }

                        float horizontalOffsetCentimeters =
                            flashlight.cameraHorizontalOffsetMeters * 100.f;
                        if (DrawSliderFloat(
                                "Horizontal Offset",
                                &horizontalOffsetCentimeters,
                                FlashlightMinimumCameraHorizontalOffsetMeters *
                                    100.f,
                                FlashlightMaximumCameraHorizontalOffsetMeters *
                                    100.f,
                                "%.1f centimeters"))
                        {
                            flashlight.cameraHorizontalOffsetMeters =
                                horizontalOffsetCentimeters * 0.01f;
                        }
                        ImGui::SetItemTooltip(
                            "Move the flashlight left or right from the camera "
                            "by up to 40 centimeters.");
                        if (DrawPresetResetIcon(
                                "Flashlight Horizontal Offset",
                                floatChanged(
                                    flashlight.cameraHorizontalOffsetMeters,
                                    defaults.cameraHorizontalOffsetMeters)))
                        {
                            flashlight.cameraHorizontalOffsetMeters =
                                defaults.cameraHorizontalOffsetMeters;
                        }

                        float verticalOffsetCentimeters =
                            flashlight.cameraVerticalOffsetMeters * 100.f;
                        if (DrawSliderFloat(
                                "Vertical Offset",
                                &verticalOffsetCentimeters,
                                FlashlightMinimumCameraVerticalOffsetMeters *
                                    100.f,
                                FlashlightMaximumCameraVerticalOffsetMeters *
                                    100.f,
                                "%.1f centimeters"))
                        {
                            flashlight.cameraVerticalOffsetMeters =
                                verticalOffsetCentimeters * 0.01f;
                        }
                        ImGui::SetItemTooltip(
                            "Move the flashlight down or up from the camera "
                            "by up to 40 centimeters.");
                        if (DrawPresetResetIcon(
                                "Flashlight Vertical Offset",
                                floatChanged(
                                    flashlight.cameraVerticalOffsetMeters,
                                    defaults.cameraVerticalOffsetMeters)))
                        {
                            flashlight.cameraVerticalOffsetMeters =
                                defaults.cameraVerticalOffsetMeters;
                        }

                        if (flashlight != flashlightBeforeControls)
                            m_app->ResetImageBasedLightingHistory();
                    }
                    else
                    {
                    const auto selectedLightIterator = std::find(
                        lights.begin(), lights.end(), m_SelectedLight);
                    const size_t selectedLightIndex = size_t(std::distance(
                        lights.begin(), selectedLightIterator));
                    const std::string defaultLightKey =
                        m_app->GetCurrentSceneName() + "\n" +
                        std::to_string(selectedLightIndex) + "\n" +
                        m_SelectedLight->GetName();
                    auto captureLightDefaults =
                        [](const Light& light)
                        {
                            LightDefaultState result;
                            result.type = light.GetLightType();
                            result.direction = light.GetDirection();
                            result.color = light.color;
                            switch (result.type)
                            {
                            case UVSR_LIGHT_TYPE_DIRECTIONAL:
                            {
                                const auto& directional =
                                    static_cast<const DirectionalLight&>(
                                        light);
                                result.irradiance =
                                    directional.irradiance;
                                result.angularSize =
                                    directional.angularSize;
                                break;
                            }
                            case UVSR_LIGHT_TYPE_POINT:
                            {
                                const auto& point =
                                    static_cast<const PointLight&>(light);
                                result.radius = point.radius;
                                result.intensity = point.intensity;
                                break;
                            }
                            case UVSR_LIGHT_TYPE_SPOT:
                            {
                                const auto& spot =
                                    static_cast<const SpotLight&>(light);
                                result.radius = spot.radius;
                                result.intensity = spot.intensity;
                                result.innerAngle = spot.innerAngle;
                                result.outerAngle = spot.outerAngle;
                                break;
                            }
                            default:
                                break;
                            }
                            return result;
                        };
                    const auto [defaultLightIterator, inserted] =
                        m_LightDefaults.try_emplace(
                            defaultLightKey,
                            captureLightDefaults(*m_SelectedLight));
                    (void)inserted;
                    const LightDefaultState& defaultLight =
                        defaultLightIterator->second;
                    const auto floatChanged =
                        [](float left, float right)
                        {
                            return std::abs(left - right) > 1e-5f;
                        };
                    const auto colorChanged =
                        [&](const float3& left, const float3& right)
                        {
                            return floatChanged(left.x, right.x) ||
                                floatChanged(left.y, right.y) ||
                                floatChanged(left.z, right.z);
                        };
                    const auto directionChanged =
                        [](const double3& left, const double3& right)
                        {
                            return std::abs(left.x - right.x) > 1e-7 ||
                                std::abs(left.y - right.y) > 1e-7 ||
                                std::abs(left.z - right.z) > 1e-7;
                        };
                    const auto drawLightColor =
                        [&](Light& light)
                        {
                            DrawUvsrColorEdit(
                                "Color",
                                &light.color.x,
                                UvsrColorEditChannels::Rgb);
                            ImGui::SetItemTooltip(
                                "Set the selected light's color.");
                            if (DrawPresetResetIcon(
                                    "Light Color",
                                    colorChanged(
                                        light.color,
                                        defaultLight.color)))
                            {
                                light.color = defaultLight.color;
                            }
                        };
                    const auto drawLightDirection =
                        [&](Light& light, bool negative)
                        {
                            double3 direction = light.GetDirection();
                            if (DrawLightDirectionSliders(
                                    direction, negative))
                            {
                                light.SetDirection(direction);
                                m_app->ResetImageBasedLightingHistory();
                            }
                            ImGui::SetItemTooltip(
                                "Set the selected light's direction.");
                            if (DrawPresetResetIcon(
                                    "Light Direction",
                                    directionChanged(
                                        light.GetDirection(),
                                        defaultLight.direction)))
                            {
                                light.SetDirection(
                                    defaultLight.direction);
                                m_app->ResetImageBasedLightingHistory();
                            }
                        };

                    switch (m_SelectedLight->GetLightType())
                    {
                    case UVSR_LIGHT_TYPE_DIRECTIONAL:
                    {
                        auto& light = static_cast<DirectionalLight&>(
                            *m_SelectedLight);
                        drawLightDirection(light, true);
                        drawLightColor(light);
                        DrawSliderFloat(
                            "Irradiance",
                            &light.irradiance,
                            0.f,
                            100.f,
                            "%.2f",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the directional light irradiance.");
                        if (DrawPresetResetIcon(
                                "Light Irradiance",
                                floatChanged(
                                    light.irradiance,
                                    defaultLight.irradiance)))
                        {
                            light.irradiance =
                                defaultLight.irradiance;
                        }
                        if (DrawBoundedSliderFloat(
                                "Angular Size",
                                &light.angularSize,
                                0.f,
                                20.f,
                                0.f,
                                10.f))
                        {
                            m_app->ResetImageBasedLightingHistory();
                        }
                        ImGui::SetItemTooltip(
                            "Set the directional light's angular diameter. Zero "
                            "degrees creates a zero-extent emitter with hard shadows.");
                        if (DrawPresetResetIcon(
                                "Light Angular Size",
                                floatChanged(
                                    light.angularSize,
                                    defaultLight.angularSize)))
                        {
                            light.angularSize =
                                defaultLight.angularSize;
                            m_app->ResetImageBasedLightingHistory();
                        }
                        break;
                    }
                    case UVSR_LIGHT_TYPE_POINT:
                    {
                        auto& light = static_cast<PointLight&>(
                            *m_SelectedLight);
                        DrawSliderFloat(
                            "Radius",
                            &light.radius,
                            0.01f,
                            1.f,
                            "%.3f",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the point light radius.");
                        if (DrawPresetResetIcon(
                                "Light Radius",
                                floatChanged(
                                    light.radius,
                                    defaultLight.radius)))
                        {
                            light.radius = defaultLight.radius;
                        }
                        drawLightColor(light);
                        DrawSliderFloat(
                            "Intensity",
                            &light.intensity,
                            0.f,
                            100.f,
                            "%.2f",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the point light intensity.");
                        if (DrawPresetResetIcon(
                                "Light Intensity",
                                floatChanged(
                                    light.intensity,
                                    defaultLight.intensity)))
                        {
                            light.intensity =
                                defaultLight.intensity;
                        }
                        break;
                    }
                    case UVSR_LIGHT_TYPE_SPOT:
                    {
                        auto& light = static_cast<SpotLight&>(
                            *m_SelectedLight);
                        drawLightDirection(light, false);
                        DrawSliderFloat(
                            "Radius",
                            &light.radius,
                            0.01f,
                            1.f,
                            "%.3f",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the spot light radius.");
                        if (DrawPresetResetIcon(
                                "Light Radius",
                                floatChanged(
                                    light.radius,
                                    defaultLight.radius)))
                        {
                            light.radius = defaultLight.radius;
                        }
                        drawLightColor(light);
                        DrawSliderFloat(
                            "Intensity",
                            &light.intensity,
                            0.f,
                            100.f,
                            "%.2f",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the spot light intensity.");
                        if (DrawPresetResetIcon(
                                "Light Intensity",
                                floatChanged(
                                    light.intensity,
                                    defaultLight.intensity)))
                        {
                            light.intensity =
                                defaultLight.intensity;
                        }
                        DrawBoundedSliderFloat(
                            "Inner Angle",
                            &light.innerAngle,
                            0.f,
                            180.f,
                            0.f,
                            90.f);
                        ImGui::SetItemTooltip(
                            "Set the full-bright spot cone angle.");
                        if (DrawPresetResetIcon(
                                "Light Inner Angle",
                                floatChanged(
                                    light.innerAngle,
                                    defaultLight.innerAngle)))
                        {
                            light.innerAngle =
                                defaultLight.innerAngle;
                        }
                        DrawBoundedSliderFloat(
                            "Outer Angle",
                            &light.outerAngle,
                            0.f,
                            180.f,
                            0.f,
                            90.f);
                        ImGui::SetItemTooltip(
                            "Set the outer spot cone angle.");
                        if (DrawPresetResetIcon(
                                "Light Outer Angle",
                                floatChanged(
                                    light.outerAngle,
                                    defaultLight.outerAngle)))
                        {
                            light.outerAngle =
                                defaultLight.outerAngle;
                        }
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

        if (BeginAnimatedToggleRegion(
                "##ShadowsDrawerVisibility",
                m_ui.Lighting == LightingSolution::RayMarching,
                UiToggleRegionOwner::Settings,
                UiToggleRegionVisualMode::ClipDuringCollapse))
        {
        const bool shadowsOpen = DrawCollapsingHeader(
            "Shadows", "Configure ray traced direct light shadows.");
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
            else if (m_app->GetActiveRasterSampleCount() > 1u)
            {
                ImGui::TextDisabled(
                    "MSAA shadows trace every covered raster sample independently.");
            }

            drawDirectionalRayShadowControls();
            EndDrawerBody();
        }
        EndAnimatedToggleRegion();
        }

        DrawMaterialDrawer(settingsControlWidth);
        DrawInterfaceDrawer(settingsControlWidth);

        TrackSettingsScrollAnchor(
            ImGui::GetID("##SettingsFooterAnchor"),
            ImGui::GetCursorScreenPos().y);
        constexpr float ActionButtonCount = 4.f;
        const float actionButtonGap = g_UiSpacingTokens.tight;
        const float actionButtonWidth = std::max(
            1.f,
            (ImGui::GetContentRegionAvail().x -
                actionButtonGap * (ActionButtonCount - 1.f)) /
                ActionButtonCount);

        const ImVec4 drawerBackgroundColor =
            g_UiVisualTokens.actionButton;
        const ImVec4 drawerBackgroundHoveredColor =
            g_UiVisualTokens.actionButtonHovered;
        const ImVec4 drawerBackgroundActiveColor =
            g_UiVisualTokens.actionButtonActive;
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            drawerBackgroundColor);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            drawerBackgroundHoveredColor);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            drawerBackgroundActiveColor);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            g_UiVisualTokens.actionButtonText);

        if (DrawCenteredActionButton("Reset", actionButtonWidth))
            ResetAllSettingsToFactoryDefaults();
        const ImVec2 actionButtonRowMinimum =
            ImGui::GetItemRectMin();
        ImGui::SetItemTooltip(
            "Restore renderer and interface factory settings without changing "
            "the camera, scene, or graphics adapter.");

        ImGui::SameLine(0.f, actionButtonGap);
        if (DrawCenteredActionButton("Capture", actionButtonWidth))
            m_ui.CopyScreenshotToClipboard = true;
        ImGui::SetItemTooltip("Copy the current frame to the clipboard.");

        ImGui::SameLine(0.f, actionButtonGap);
        if (DrawCenteredActionButton(
                GetPixelZoomButtonLabel(m_ui.PixelZoom),
                actionButtonWidth))
        {
            m_ui.PixelZoom =
                AdvancePixelZoomMode(m_ui.PixelZoom);
        }
        ImGui::SetItemTooltip(
            "Cycle exact Off, 2x, 3x, 4x, and 5x pixel zoom. Z uses the "
            "same cycle.");

        ImGui::SameLine(0.f, actionButtonGap);
        if (DrawCenteredActionButton("Restart", actionButtonWidth))
        {
            g_RestartRequested = true;
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), GLFW_TRUE);
        }
        const ImVec2 actionButtonRowMaximum =
            ImGui::GetItemRectMax();
        if (g_UiVisualTokens.sceneTranslucentHeaders)
        {
            g_SettingsScrollStabilityContext
                .translucentHeaderSupportRects.push_back(
                    ImRect(
                        actionButtonRowMinimum,
                        actionButtonRowMaximum));
        }
        ImGui::SetItemTooltip("Restart UVSR.");
        ImGui::PopStyleColor(4);

        if (settingsScrollInputBlocked)
        {
            ImGui::EndDisabled();
            ImGui::PopUvsrDisabledPresentation();
            ImGui::PopStyleVar();
        }
        if (deferredDropdownInputBlocked)
        {
            ImGui::EndDisabled();
            ImGui::PopUvsrDisabledPresentation();
            ImGui::PopStyleVar();
        }

        EndSettingsScrollStability();
        settingsScrollIdle =
            settingsBodyWindow->ScrollTarget.y >= FLT_MAX;
        settingsLayoutIdle =
            !g_SettingsScrollStabilityContext
                .layoutAnimatingLastFrame;
        const ImRect settingsBodyViewportRect(
            settingsBodyWindow->Pos,
            ImVec2(
                settingsBodyWindow->Pos.x +
                    settingsBodyWindow->Size.x,
                settingsBodyWindow->Pos.y +
                    settingsBodyWindow->Size.y));
        ImGuiWindow* settingsRootWindow =
            settingsBodyWindow->ParentWindow;
        const ImRect settingsRootBodyRect(
            ImVec2(
                settingsRootWindow->Pos.x,
                settingsRootWindow->Pos.y +
                    settingsRootWindow->TitleBarHeight),
            ImVec2(
                settingsRootWindow->Pos.x +
                    settingsRootWindow->Size.x,
                settingsRootWindow->Pos.y +
                    settingsRootWindow->Size.y));
        ImRect settingsAnimatedContentRect(
            ImLerp(
                settingsBodyViewportRect.Min,
                settingsRetainedContentRect.Min,
                settingsCollapseAmount),
            ImLerp(
                settingsBodyViewportRect.Max,
                settingsRetainedContentRect.Max,
                settingsCollapseAmount));
        settingsAnimatedContentRect.Min.x = std::max(
            settingsAnimatedContentRect.Min.x,
            settingsRootBodyRect.Min.x + style.WindowPadding.x);
        settingsAnimatedContentRect.Min.y = std::max(
            settingsAnimatedContentRect.Min.y,
            settingsRootBodyRect.Min.y + style.WindowPadding.y);
        settingsAnimatedContentRect.Max.x = std::min(
            settingsAnimatedContentRect.Max.x,
            settingsRootBodyRect.Max.x - style.WindowPadding.x);
        settingsAnimatedContentRect.Max.y = std::min(
            settingsAnimatedContentRect.Max.y,
            settingsRootBodyRect.Max.y - style.WindowPadding.y);
        ImDrawList* settingsDecorationDrawList =
            ResolveFinalSettingsDecorationDrawList(settingsRootWindow);
        if (settingsDecorationDrawList &&
            settingsDecorationDrawList->_Splitter._Count > 1)
        {
            settingsDecorationDrawList->ChannelsMerge();
        }
        // Dear ImGui submits a parent before its recursively ordered visible
        // descendants. Append one continuously morphing Settings surface and
        // outline to that completed final list. The same rectangle starts at
        // the scrolling viewport, follows the animated root, and reaches the
        // retained hash perimeter without an opacity or geometry swap.
        DrawRootPanelBodyChrome(
            settingsDecorationDrawList,
            settingsRootBodyRect,
            settingsAnimatedContentRect,
            settingsRetainedContentRect,
            style.WindowRounding);
        if (expandedSettingsSnapshotSubmitted)
        {
            settingsDecorationDrawList->PushClipRect(
                settingsRootBodyRect.Min,
                settingsRootBodyRect.Max,
                false);
            settingsDecorationDrawList->AddText(
                expandedSettingsSnapshotMinimum,
                ImGui::GetColorU32(ImGuiCol_Text),
                m_SettingsSnapshots.Code().c_str());
            settingsDecorationDrawList->PopClipRect();
        }
        ImGui::PopUvsrColorPickerPopupContentRight();
        ImGui::EndChild();
        }
        const ImVec2 settingsWindowPosition =
            ImGui::GetWindowPos();
        const ImVec2 settingsWindowSize =
            ImGui::GetWindowSize();
        const float settingsTitleHeight =
            settingsHeaderWindow->TitleBarHeight;
        const float rootBodyRounding =
            style.WindowRounding;
        const ImRect settingsBodyRect = settingsHeaderBodyRect;
        if (settingsBodySubmitted &&
            g_UiVisualTokens.sceneTranslucentHeaders)
        {
            DrawTranslucentHeaderPanelBodySurface(
                settingsWindowDrawList,
                settingsBodyRect,
                rootBodyRounding);
        }
        if (settingsCollapsed &&
            settingsBodyRect.GetHeight() >
                style.WindowPadding.y * 2.f + 2.f)
        {
            const ImRect snapshotHitRect = DrawCompactRootPanelBody(
                settingsWindowDrawList,
                settingsBodyRect,
                settingsRetainedContentRect,
                rootBodyRounding,
                m_SettingsSnapshots.Code().c_str());
            const bool snapshotHovered =
                (settingsWindowFlags & ImGuiWindowFlags_NoInputs) == 0 &&
                snapshotHitRect.Contains(ImGui::GetIO().MousePos);
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
        const bool settingsCollapseTransitionActive =
            ImGui::IsCurrentUvsrWindowCollapseTransitionActive();
        settingsLayoutIdle =
            settingsLayoutIdle &&
            !settingsCollapseTransitionActive;
        m_SettingsCollapsed = settingsCollapsed;
        if (settingsCollapsed)
        {
            const bool materialPresentationWasActive =
                m_ui.ShowMaterialDrawer ||
                m_MaterialDrawerAppearance > 0.f;
            if (m_ui.ShowMaterialDrawer)
                RequestMaterialDrawerVisible(false);
            m_MaterialDrawerAppearance = 0.f;
            m_MaterialDrawerPresentationForceClosed =
                m_MaterialDrawerPresentationForceClosed ||
                materialPresentationWasActive;
        }
        UiBackdropRect& performanceTitleBackdrop =
            m_ui.BackdropRects[UiPerformanceTitleBackdropIndex];
        UiBackdropRect& performanceBodyBackdrop =
            m_ui.BackdropRects[UiPerformanceBodyBackdropIndex];
        UiBackdropRect& settingsTitleBackdrop =
            m_ui.BackdropRects[UiSettingsTitleBackdropIndex];
        UiBackdropRect& settingsBodyBackdrop =
            m_ui.BackdropRects[UiSettingsBodyBackdropIndex];
        CapturePanelSurfaceBackdrops(
            performanceTitleBackdrop,
            performanceBodyBackdrop,
            performanceWindowPosition,
            performanceWindowSize,
            settingsTitleHeight,
            performanceWindowSize.y > settingsTitleHeight + 0.5f,
            style.FrameRounding,
            rootBodyRounding);
        CapturePanelSurfaceBackdrops(
            settingsTitleBackdrop,
            settingsBodyBackdrop,
            settingsWindowPosition,
            settingsWindowSize,
            settingsTitleHeight,
            settingsWindowSize.y > settingsTitleHeight + 0.5f,
            style.FrameRounding,
            rootBodyRounding);
        ImGui::End();

        const ImVec2 panelStackCenter(
            performanceWindowPosition.x +
                performanceWindowSize.x * 0.5f,
            (performanceWindowPosition.y +
                settingsWindowPosition.y +
                settingsWindowSize.y) * 0.5f);
        if (g_UiVisualTokens.sceneTranslucentHeaders)
        {
            const float supportInset = std::max(
                1.f,
                style.FrameRounding);
            for (const ImRect& headerRect :
                g_SettingsScrollStabilityContext
                    .translucentHeaderSupportRects)
            {
                const UiBackdropExclusionRect exclusion = {
                    headerRect.Min.x + supportInset,
                    headerRect.Min.y + supportInset,
                    headerRect.Max.x - supportInset,
                    headerRect.Max.y - supportInset
                };
                if (exclusion.maxX > exclusion.minX &&
                    exclusion.maxY > exclusion.minY)
                {
                    settingsBodyBackdrop.compositeExclusions.push_back(
                        exclusion);
                }
            }
        }
        TrackSettingsAppearanceDrawList(
            ImGui::GetUvsrActiveColorPickerPopupDrawList());
        for (size_t backdropIndex =
                UiPerformanceTitleBackdropIndex;
            backdropIndex <= UiSettingsBodyBackdropIndex;
            ++backdropIndex)
        {
            ApplyBackdropAppearance(
                m_ui.BackdropRects[backdropIndex],
                panelStackCenter,
                settingsAppearanceScale,
                settingsAppearanceOpacity);
        }
        ApplyWindowAppearance(
            performanceWindowDrawList,
            panelStackCenter,
            settingsAppearanceScale,
            settingsAppearanceOpacity);
        for (ImDrawList* drawList :
            g_PerformanceAppearanceDrawLists)
        {
            ApplyWindowAppearance(
                drawList,
                panelStackCenter,
                settingsAppearanceScale,
                settingsAppearanceOpacity);
        }
        ApplyWindowAppearance(
            settingsWindowDrawList,
            panelStackCenter,
            settingsAppearanceScale,
            settingsAppearanceOpacity);
        for (ImDrawList* drawList :
            g_SettingsAppearanceDrawLists)
        {
            ApplyWindowAppearance(
                drawList,
                panelStackCenter,
                settingsAppearanceScale,
                settingsAppearanceOpacity);
        }
        ImGui::PopStyleColor(4);

        // Commit only after every UI window has finished composing. Any
        // synchronous renderer work then holds a previously presented stable
        // frame instead of interrupting popup, drawer, scroll, Settings, or
        // magnifier motion.
        FinishUnsubmittedDeferredDropdownPopupTransition();
        TryApplyDeferredDropdownUiActions(
            deferredDropdownCompositionIdle(
                settingsLayoutIdle &&
                    !performanceCollapseTransitionActive &&
                    !g_PerformanceTableTransitionActive,
                settingsScrollIdle && performanceScrollIdle));
        RestoreActiveUiWordSpacing();
        ImGui::PopFont();
    }

