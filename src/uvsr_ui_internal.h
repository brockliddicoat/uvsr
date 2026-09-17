#pragma once

/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "uvsr_scene_viewer.h"
#include "uvsr_application.h"
#include "uvsr_runtime.h"
#include "uvsr_settings_commands.h"
#include "renderer_common_passes_nvrhi.h"
#include "renderer_shader_factory_nvrhi.h"
#include "renderer_statistics.h"
#include "renderer_gpu_contract.h"
#include "build_identity.h"
#include "scene_catalog.h"
#include "scene_loading.h"
#include "settings_value.h"
#include "windows_executable_path.h"
#include "settings_snapshot_decoder.h"
#include "settings_snapshot.h"
#include "settings_snapshot_transaction.h"
#include "ui_layout.h"
#include "ui_settings_command_catalog.h"
#include "ui_performance_timing_rows.h"
#include "path_tracing_pass_nvrhi.h"
#include "renderer_log.h"
#include "display_sync_test.h"
#if defined(UVSR_BUILD_TESTING)
#include "retained_runtime_diagnostic.h"
#endif
#include "renderer_ui_context.h"
#include "renderer_ui_nvrhi.h"
#include "renderer_ui_input_glfw.h"
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include "ui_light_defaults.h"
#include <utility>
#include <type_traits>
#include <initializer_list>

using namespace donut;
using namespace donut::app;
using namespace uvsr;

namespace uvsr_detail
{
    inline bool AcceptFormattedSelector(bool formatted, const UiSettingsValue& value,
        SettingsSnapshotError& error) noexcept
    {
        if (formatted) return true;
        // existing UI reads and actions consume an empty selector for malformed
        // source identities. checked storage failures must still reach the caller.
        if (error.code == SettingsSnapshotErrorCode::InvalidInput &&
            value.kind == UiSettingsValueKind::Selector && value.Text().empty())
        {
            error = {};
            return true;
        }
        return false;
    }

    struct alignas(16) PixelZoomConstants
    {
        gpu_contract::Uint2 sourceSize;
        gpu_contract::Uint2 panelMin;

        gpu_contract::Uint2 panelSize;
        uint32_t zoomFactor = 0u;
        float cornerRadius = 8.f;

        float opacity = 0.f;
        float outlineWidth = 1.5f;
        float shadowBlur = 10.f;
        float shadowOpacity = 0.34f;

        float shadowOffsetY = 3.f;
        gpu_contract::Float3 padding;

        gpu_contract::Float4 outlineTopColor;
        gpu_contract::Float4 outlineBottomColor;
    };

    static_assert(sizeof(PixelZoomConstants) == 96u);

    class PixelZoomPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
        uvsr::RendererCommonPasses* m_CommonPasses = nullptr;
        nvrhi::CommandListHandle m_CommandList;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::TextureHandle m_SourceTexture;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::GraphicsPipelineHandle m_Pipeline;
        uint32_t m_WindowWidth = 0u;
        uint32_t m_WindowHeight = 0u;
        nvrhi::Format m_FramebufferFormat = nvrhi::Format::UNKNOWN;
        bool m_CapturedFrame = false;

        void ResetResources()
        {
            m_SourceTexture = nullptr;
            m_BindingSet = nullptr;
            m_Pipeline = nullptr;
            m_WindowWidth = 0u;
            m_WindowHeight = 0u;
            m_FramebufferFormat = nvrhi::Format::UNKNOWN;
            m_CapturedFrame = false;
        }

        bool EnsureResources(nvrhi::IFramebuffer* framebuffer)
        {
            if (!framebuffer || !m_Device || !m_CommonPasses || !m_CommonPasses->IsValid() ||
                !m_CommandList || !m_PixelShader || !m_BindingLayout || !m_ConstantBuffer)
                return false;
            const nvrhi::FramebufferInfoEx& framebufferInfo =
                framebuffer->getFramebufferInfo();
            if (framebufferInfo.colorFormats.empty())
                return false;

            const uint32_t windowWidth = framebufferInfo.width;
            const uint32_t windowHeight = framebufferInfo.height;
            const nvrhi::Format framebufferFormat =
                framebufferInfo.colorFormats[0];
            if (m_SourceTexture &&
                m_WindowWidth == windowWidth &&
                m_WindowHeight == windowHeight &&
                m_FramebufferFormat == framebufferFormat)
            {
                return true;
            }

            ResetResources();
            if (windowWidth == 0u ||
                windowHeight == 0u ||
                !m_PixelShader)
            {
                return false;
            }

            m_WindowWidth = windowWidth;
            m_WindowHeight = windowHeight;
            m_FramebufferFormat = framebufferFormat;

            nvrhi::TextureDesc sourceDesc;
            sourceDesc.width = windowWidth;
            sourceDesc.height = windowHeight;
            sourceDesc.dimension = nvrhi::TextureDimension::Texture2D;
            sourceDesc.mipLevels = 1u;
            sourceDesc.format = framebufferFormat;
            sourceDesc.initialState = nvrhi::ResourceStates::ShaderResource;
            sourceDesc.keepInitialState = true;
            sourceDesc.debugName = "Pixel Zoom/Unmodified Presented Frame";
            m_SourceTexture = m_Device->createTexture(sourceDesc);

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_SourceTexture)
            };
            m_BindingSet = m_Device->createBindingSet(
                bindingSetDesc,
                m_BindingLayout);

            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
            pipelineDesc.VS =
                m_CommonPasses->FullscreenVertexShader();
            pipelineDesc.PS = m_PixelShader;
            pipelineDesc.bindingLayouts = { m_BindingLayout };
            pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
            pipelineDesc.renderState.depthStencilState.stencilEnable = false;
            pipelineDesc.renderState.blendState.targets[0]
                .setBlendEnable(true)
                .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
                .setDestBlendAlpha(nvrhi::BlendFactor::One);
            m_Pipeline = m_Device->createGraphicsPipeline(
                pipelineDesc,
                framebufferInfo);

            return
                m_SourceTexture &&
                m_BindingSet &&
                m_Pipeline;
        }

    public:
        PixelZoomPass(
            nvrhi::IDevice* device,
            uvsr::RendererShaderFactory* shaderFactory,
            uvsr::RendererCommonPasses* commonPasses)
            : m_Device(device)
            , m_CommonPasses(commonPasses)
        {
            if (!device || !shaderFactory || !commonPasses) return;
            m_CommandList = device->createCommandList();
            m_PixelShader = shaderFactory->CreateShader(
                "uvsr/pixel_zoom_ps.hlsl",
                "main",
                {},
                nvrhi::ShaderType::Pixel);

            nvrhi::BufferDesc constantBufferDesc;
            constantBufferDesc.byteSize = sizeof(PixelZoomConstants);
            constantBufferDesc.debugName = "Pixel Zoom/Constants";
            constantBufferDesc.isConstantBuffer = true;
            constantBufferDesc.isVolatile = true;
            constantBufferDesc.maxVersions =
                uvsr::RendererMaxConstantBufferVersions;
            m_ConstantBuffer =
                device->createBuffer(constantBufferDesc);

            nvrhi::BindingLayoutDesc bindingLayoutDesc;
            bindingLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
            bindingLayoutDesc.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0)
            };
            m_BindingLayout =
                device->createBindingLayout(bindingLayoutDesc);
        }

        void BackBufferResizing()
        {
            ResetResources();
        }

        bool Capture(nvrhi::IFramebuffer* framebuffer)
        {
            m_CapturedFrame = false;
            if (!EnsureResources(framebuffer))
                return false;

            nvrhi::ITexture* framebufferTexture =
                framebuffer->getDesc().colorAttachments[0].texture;
            m_CommandList->open();
            m_CommandList->beginMarker("Pixel Zoom Capture");
            m_CommandList->copyTexture(
                m_SourceTexture,
                nvrhi::TextureSlice(),
                framebufferTexture,
                nvrhi::TextureSlice());
            m_CommandList->endMarker();
            m_CommandList->close();
            m_Device->executeCommandList(m_CommandList);
            m_CapturedFrame = true;
            return true;
        }

        void Composite(
            nvrhi::IFramebuffer* framebuffer,
            PixelZoomMode mode,
            uint32_t panelMarginPixels,
            float cornerRadius)
        {
            if (!m_CapturedFrame || !IsPixelZoomEnabled(mode))
            {
                return;
            }

            const PixelZoomLayout layout = ResolvePixelZoomLayout(
                m_WindowWidth, m_WindowHeight, panelMarginPixels, mode);
            if (layout.panelWidth == 0u ||
                layout.panelHeight == 0u ||
                layout.panelMinX + layout.panelWidth > m_WindowWidth ||
                layout.panelMinY + layout.panelHeight > m_WindowHeight)
            {
                m_CapturedFrame = false;
                return;
            }

            PixelZoomConstants constants{};
            constants.sourceSize = {
                layout.sourceWidth,
                layout.sourceHeight};
            constants.panelMin = {
                layout.panelMinX,
                layout.panelMinY};
            constants.panelSize = {
                layout.panelWidth,
                layout.panelHeight};
            constants.zoomFactor = layout.zoomFactor;
            constants.cornerRadius = cornerRadius;
            constants.opacity = 1.f;
            // A centered one-pixel ImGui stroke fully covers its edge texels.
            // The 1.5-pixel signed-distance band reproduces that visual weight
            // without filtering the magnified interior.
            constants.outlineWidth = 1.5f;
            constants.outlineTopColor =
                {0.88f, 0.90f, 0.94f, 0.10f};
            constants.outlineBottomColor =
                {0.96f, 0.97f, 1.00f, 0.30f};

            const float shadowExtent =
                std::ceil(
                    constants.shadowBlur +
                    constants.shadowOffsetY);
            const float minX = std::max(
                0.f,
                float(layout.panelMinX) - shadowExtent);
            const float minY = std::max(
                0.f,
                float(layout.panelMinY) - shadowExtent);
            const float maxX = std::min(
                float(m_WindowWidth),
                float(layout.panelMinX + layout.panelWidth) +
                    shadowExtent);
            const float maxY = std::min(
                float(m_WindowHeight),
                float(layout.panelMinY + layout.panelHeight) +
                    shadowExtent);
            const nvrhi::Viewport panelViewport(
                minX,
                maxX,
                minY,
                maxY,
                0.f,
                1.f);

            m_CommandList->open();
            m_CommandList->beginMarker("Pixel Zoom Composite");
            m_CommandList->writeBuffer(
                m_ConstantBuffer,
                &constants,
                sizeof(constants));

            nvrhi::GraphicsState graphicsState;
            graphicsState.pipeline = m_Pipeline;
            graphicsState.framebuffer = framebuffer;
            graphicsState.bindings = { m_BindingSet };
            graphicsState.viewport.addViewport(panelViewport);
            graphicsState.viewport.addScissorRect(
                nvrhi::Rect(panelViewport));
            m_CommandList->setGraphicsState(graphicsState);

            nvrhi::DrawArguments drawArguments;
            drawArguments.instanceCount = 1;
            drawArguments.vertexCount = 4;
            m_CommandList->draw(drawArguments);

            m_CommandList->endMarker();
            m_CommandList->close();
            m_Device->executeCommandList(m_CommandList);
            m_CapturedFrame = false;
        }
    };
}

using namespace uvsr_detail;

class UIRenderer : public donut::app::IRenderPass
{
private:
    enum class StatisticsEffect : int
    {
        CompleteRenderer,
        SceneSetup,
        Geometry,
        PathTransport,
        DirectLighting,
        Shadows,
        FastApproximate,
        MaterialPicking,
        EnvironmentBackground,
        ToneMapping,
        OutputBlit,
        Count
    };

    inline static const ImVec4 UiErrorColor{0.26f, 0.59f, 0.98f, 0.31f};
    inline static const ImVec4 UiSuccessColor{0.117f, 0.217f, 0.342f, 1.f};

    struct FrontEllipsisText
    {
        // ImGui consumes at most four bytes per code point. the tooltip uses 117.
        char display[4u * 117u + 3u + 1u]{};
        bool truncated = false;
    };

    template<size_t MaximumCodePoints>
    [[nodiscard]] static FrontEllipsisText FormatFrontEllipsisUtf8(
        std::string_view source) noexcept {
        static_assert(MaximumCodePoints <= 117u);
        FrontEllipsisText result;
        if (source.empty()) return result;
        const char* const begin = source.data();
        const char* cursor = begin;
        const char* const end = begin + source.size();
        size_t codePointCount = 0;
        while (cursor < end && codePointCount < MaximumCodePoints)
        {
            unsigned int codePoint = 0;
            const int byteCount = ImTextCharFromUtf8(
                &codePoint,
                cursor,
                end);
            cursor += byteCount > 0 ? byteCount : 1;
            ++codePointCount;
        }

        result.truncated = cursor < end;
        const size_t bytes = size_t(cursor - begin);
        if (bytes) std::memcpy(result.display, begin, bytes);
        if (result.truncated)
            std::memcpy(result.display + bytes, "...", 3u);
        return result;
    }

    // the application destroys UI before this viewer and its helpers.
    UvsrSceneViewer* m_app;

    uvsr::RendererUiContext m_UiContext;
    uvsr::RendererUiNvrhi m_UiGpu;
    bool m_UiGpuReady = false;
    bool m_RequiredFontsReady = false;
    const char* m_RequiredFontFailure = nullptr;
    uvsr::RendererSceneHandle m_SelectedLight;
    double m_DisplayedFrameTime = 0.0;
    double m_StatSnapshotElapsed = 0.0;
    double m_StatFrameTimeSum = 0.0;
    uint32_t m_StatFrameTimeCount = 0;
    struct PerformanceStatText
    {
        char resolution[512]{};
        char milliseconds[512]{};
        char framesPerSecond[512]{};
        char triangles[32]{};
        // three stat limits, the triangle limit, three separators and a terminator.
        char line[3u * 511u + 31u + 9u + 1u]{};
    };
    PerformanceStatText m_PerformanceStatText;
    uvsr::PerformanceTimingRowRetention m_PerformanceTimingRows;
    bool m_HasAppliedStatSnapshot = false;
    bool m_WasSceneLoading = false;
    bool m_SceneLoadFailed = false;
    std::chrono::steady_clock::time_point m_SceneLoadCounterStart;
    SettingsSnapshotText m_SceneLoadHistoryKey;
    SceneLoadTimingDatabase m_SceneLoadTiming;

    [[nodiscard]] static bool GetSceneLoadTimingDatabasePath(
        WindowsPath& output, WindowsPathResult& result) noexcept;

    void LoadSceneLoadTimingDatabase();

    void SaveSceneLoadTimingDatabase() const;
    std::unique_ptr<PixelZoomPass> m_PixelZoomPass;
    uint32_t m_SettingsPanelMarginPixels =
        static_cast<uint32_t>(UiSpacingBasePixels * 4.f);
    float m_UiDisplayScale = 1.f;
    bool m_SettingsCollapsed = false;
    DisplaySyncTestSettings m_DisplaySyncSettings;
    HANDLE m_PresentationWaitTimer = nullptr;
    DisplayPresentationSettings m_PreviousPresentationSettings = DefaultDisplayPresentationSettings;
    double m_DisplaySyncStart = 0.0;
    double m_PresentationLastFrame = 0.0;
    double m_PresentationDeadline = 0.0;
    double m_PresentationTargetFps = 0.0;
    double m_PresentationRefreshRate = 0.0;
    std::array<float, 240> m_PresentationIntervals{};
    size_t m_PresentationIntervalCount = 0;
    size_t m_PresentationIntervalOffset = 0;
    void SetDisplaySyncTestActive(bool active);
    void DrawDisplaySyncTest();
    void ResetPresentationTiming();
    void AdvanceDisplayPresentation(float elapsedTimeSeconds);
    void DrawDeveloperDrawer(float controlWidth);
    void DrawPostprocessDrawer(float controlWidth);
    float m_SettingsScrollY = 0.f;
    int m_SettingsScrollFrame = -1;
    SettingsSnapshotController m_SettingsSnapshots;
    // startup text borrows process argv, which outlives this UI owner.
    std::string_view m_StartupSettingsSnapshotCode;
    bool m_StartupSettingsSnapshotAttempted = false;
    SettingsSnapshotText m_PendingSettingsSnapshotCode;
#if defined(UVSR_BUILD_TESTING)
    bool SelectRuntimeDiagnostic(SettingId id, const UiSettingsValue& selector,
        const char* emptyError, const char* failurePrefix, SettingsSnapshotError& error);
    bool m_SettingsContractDiagnosticComplete = false;
    std::unique_ptr<RetainedRuntimeDiagnosticState>
        m_RetainedRuntimeDiagnostic;
    RetainedRuntimeProvenance m_RetainedRuntimeProvenance;
    std::chrono::steady_clock::time_point m_RetainedRuntimeStartup;
    std::optional<RetainedRuntimeCameraPose>
        m_RetainedRuntimeBaselineCamera;
    int m_RetainedRuntimeBaselineWidth = 0;
    int m_RetainedRuntimeBaselineHeight = 0;
    RetainedRuntimeAction m_LastRetainedRuntimeAction =
        RetainedRuntimeAction::None;
    bool m_RetainedRuntimePathReselectionPending = false;
    std::optional<RetainedRuntimeCase::Setting> m_RetainedRuntimePrerequisiteRestore;
    uint32_t m_RetainedRuntimePrerequisiteFrames = 0;
#endif
    std::optional<bool> m_SettingsCollapsedRequest;
    std::optional<bool> m_PerformanceCollapsedRequest;
    bool m_PathingDrawerOpenRequested = false;
    bool m_MaterialRevealRequested = false;
    int m_StatisticsEffect =
        static_cast<int>(StatisticsEffect::CompleteRenderer);

    UiLightDefaultsCache m_LightDefaults;

	UIData& m_ui;
    inline static UiSpacingTokens g_UiSpacingTokens;

    static void ApplyUiStyle(float displayScale);

    struct RootPanel
    {
        ImGuiWindow* window;
        bool expanded;
        bool collapsed;
    };
    struct RootPanelGeometry
    {
        ImRect body, content, summary;
    };
    RootPanel BeginRootPanel(bool performance, const ImVec2& position, float width, float maximumHeight);
    static void EndRootPanel();
    static RootPanelGeometry GetRootPanelGeometry(const ImGuiWindow* window);

    static float GetSettingsCollapsedWindowHeight(
        const ImGuiStyle& style,
        float fontSize);

    bool DrawCollapsingHeader(
        const char* label,
        const char* tooltip,
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None);

    static void BeginDrawerBody(
        const char* id,
        float controlWidth,
        float maximumHeight = 0.f);

    static ImRect DrawCompactRootPanelBody(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& contentRect,
        float rounding,
        const char* text);

    static void EndDrawerBody();

    struct NestedDrawerContext
    {
        ImGuiWindow* bodyWindow;
        float indentSpacing;
    };
    // FXAA and FXAA Tuning are the deepest retained settings tree.
    static constexpr size_t MaximumNestedDrawerDepth = 2;
    NestedDrawerContext m_NestedDrawerContexts[MaximumNestedDrawerDepth]{};
    size_t m_NestedDrawerCount = 0;

    static void BeginControlRegion(ImGuiID id);
    static void EndControlRegion();
    bool BeginSettingsTree(const char* label,
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None, const char* tooltip = nullptr);
    void EndSettingsTree();
    static bool BeginToggleRegion(const char* id, bool visible);

    static void DrawMaterialEditorName(uint32_t selectionId, std::string_view name);

    static void DrawMaterialEditorTextureFilename(
        std::string_view filename,
        const gpu_contract::Float4& color);

    static void SetNextLabeledControlWidth(
        const char* label,
        float preferredWidth);

    bool DrawPresetResetIcon(const char* id, bool modified,
        const char* tooltip = "Reset this setting to its default value.", bool nested = false);

    template<typename Action>
    static void DrawDropdownOption(
        const char* label, bool selected, Action action)
    {
        // Reselecting the current choice must not normalize hidden fields or
        // rebuild renderer resources.
        if (ImGui::Selectable(label, selected) && !selected)
            action();
    }


    bool IsCommandRuntimeMutationLocked(
        const UiSettingsCommandDefinition& definition) const;

    bool CheckCommandMutationAllowed(
        const UiSettingsCommandDefinition& definition,
        SettingsSnapshotError& error) const;

    void RequestMaterialDrawerVisible(bool visible);

    const GpuAdapterChoice* GetActiveGpuAdapterChoice() const;

    void ApplyLightingSolution(
        LightingSolution solution,
        bool invalidateHistory = true);

    [[nodiscard]] bool ResetAllSettingsToFactoryDefaults(
        SettingsSnapshotError& error);

    [[nodiscard]] bool RunAction(
        ActionId id,
        SettingsSnapshotError& error);

    uvsr::RendererSceneHandle GetDefaultCommandLight() const;

    uvsr::RendererSceneHandle EnsureCommandSelectedLight();

    [[nodiscard]] bool GetCommandLightDefaults(RendererSceneHandle light,
        UiLightDefaults& output, SettingsSnapshotError& error);

    static bool IsCommandMaterialTransmissive(RendererMaterialDomain domain);

    static bool IsCommandMaterialAlphaTested(RendererMaterialDomain domain);

    static bool IsCommandMaterialAlphaBlended(RendererMaterialDomain domain);

    bool DispatchTypedSetting(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue* requested,
        UiSettingsValue& value,
        SettingsSnapshotError& error,
        bool allowLatentMutation = false,
        bool deferMutationEffects = false,
        SettingsSnapshotErrorCode* valueFailure = nullptr);

    [[nodiscard]] bool ResolveSettingDefaultValue(
        const UiSettingsCommandDefinition& definition,
        UiSettingsValue& value,
        SettingsSnapshotError& error);

    void ApplySettingMutationEffects(
        const UiSettingsCommandDefinition& definition);

    [[nodiscard]] bool ReadSettingValue(
        SettingId id,
        SettingsSnapshotText& value,
        SettingsSnapshotError& error);

    [[nodiscard]] bool ReadSettingValue(
        SettingId id,
        UiSettingsValue& value,
        SettingsSnapshotError& error);

    [[nodiscard]] bool ApplySettingValue(
        SettingId id,
        std::string_view canonicalValue,
        SettingsSnapshotError& error);

    [[nodiscard]] bool ApplySettingValue(
        SettingId id,
        const UiSettingsValue& value,
        SettingsSnapshotError& error,
        bool deferMutationEffects = false);

    [[nodiscard]] bool ResetSettingValue(
        SettingId id,
        SettingsSnapshotError& error);

    [[nodiscard]] bool IsSettingAvailable(SettingId id) const;

    [[nodiscard]] bool IsSettingAtContextualDefault(
        SettingId id,
        SettingsSnapshotError& error);

    [[nodiscard]] bool ValidateSettingsSnapshotValue(SettingId id, std::string_view requestedValue,
        std::string_view dependencySelector, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool ReadSettingsSnapshotValue(SettingId id, bool raw, SettingsSnapshotText& value,
        SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool WriteSettingsSnapshotValue(SettingId id, std::string_view requestedValue,
        SettingsSnapshotError& error) noexcept;
    [[nodiscard]] SettingsSnapshotSelectorTransition DriveSettingsSnapshotSelector(SettingId id,
        std::string_view canonicalToken, bool begin, bool rollback, SettingsSnapshotError& error) noexcept;

    [[nodiscard]] SettingsSnapshotRuntimeAccess
    MakeSettingsSnapshotRuntimeAccess();

    bool RefreshSettingsSnapshot();

    void CopySettingsSnapshot();

    // callers retain terminated argv, checked text or fixed diagnostics through the call.
    void FailStartupSettingsSnapshot(std::string_view code, std::string_view error);

    void HandleStagedSettingsSnapshotStep(
        const SettingsSnapshotTransactionStep& step);

    void TryApplyStartupSettingsSnapshot();

    void DrawPerformancePanelContents(
        float settingsControlWidth,
        const char* performanceLine);

    UiSettingsValue ReadUiPresentationValue(SettingId id);
    bool ApplyUiSetting(SettingId id, const UiSettingsValue& value);
    bool ApplyUiSettingText(SettingId id, std::string_view text);
    bool ResetUiSetting(SettingId id);
    bool IsUiSettingChanged(SettingId id);
    std::size_t ReadUiTokenIndex(SettingId id);
    bool DrawUiReset(SettingId id, bool nested = false, const char* resetId = nullptr,
        const char* tooltip = "Reset this setting to its default value.");
    void DrawUiBoolean(const char* label, SettingId id, const char* tooltip,
        bool nestedReset = false, const std::string_view* texturePath = nullptr, const char* resetId = nullptr);
    void DrawUiFloat(const char* label, SettingId id, const char* format,
        const char* tooltip, ImGuiSliderFlags flags = ImGuiSliderFlags_None,
        bool nestedReset = false, float width = 0.f, bool useContextMaximum = false);
    void DrawUiInteger(const char* label, SettingId id, const char* format,
        const char* tooltip, ImGuiSliderFlags flags = ImGuiSliderFlags_None, bool nestedReset = false);
    void DrawUiInheritedNumber(const char* label, SettingId id, const UiSettingsValue& inherited,
        const char* format, const char* tooltip);
    void DrawUiColor(const char* label, SettingId id, const char* tooltip,
        const char* resetId = nullptr, float width = 0.f, bool enabled = true);
    void DrawUiTokenOption(SettingId id, std::size_t index, bool selected);
    void DrawUiRoundedTokenCombo(const char* label, SettingId id,
        bool custom = false, bool focusSelected = true);
    void DrawUiTokenCombo(const char* label, SettingId id, const char* tooltip, bool nestedReset = false);

    void DrawGeneralDrawer(float settingsControlWidth);

    void DrawPathingDrawer(float settingsControlWidth);

    void DrawMaterialDrawer(float settingsControlWidth);

    template <typename... Arguments>
    [[nodiscard]] static bool FormatStatLine(
        char (&destination)[512],
        const char* format,
        Arguments... arguments) noexcept
    {
        return snprintf(destination, sizeof(destination), format, arguments...) >= 0;
    }

    [[nodiscard]] static bool FormatSliderInput(
        char (&output)[sizeof("%.1f centimeters")], const char* format) noexcept
    {
        if (!format) return false;
        size_t bytes = 0;
        while (bytes < sizeof(output) && format[bytes]) ++bytes;
        if (bytes == sizeof(output)) return false;
        std::memcpy(output, format, bytes + 1u);
        const char* const words[] = {" degrees", " centimeters", " candela"};
        const char* const units[] = {"\xC2\xB0", " cm", " cd"};
        for (size_t index = 0; index < sizeof(words) / sizeof(words[0]); ++index)
        {
            char* const found = std::strstr(output, words[index]);
            if (!found) continue;
            const size_t wordBytes = std::strlen(words[index]);
            const size_t unitBytes = std::strlen(units[index]);
            std::memmove(found + unitBytes, found + wordBytes,
                bytes - size_t(found - output) - wordBytes + 1u);
            std::memcpy(found, units[index], unitBytes);
            bytes -= wordBytes - unitBytes;
        }
        return true;
    }

    void UpdateStatSnapshot(int width, int height);

    template<typename Number>
    bool DrawBoundedSlider(const char* label, Number* value,
        Number logicalMinimum, Number logicalMaximum, Number travelMinimum, Number travelMaximum,
        const char* format = std::is_same_v<Number, int> ? "%d" : "%.3f", ImGuiSliderFlags flags = 0)
    {
        static_assert(std::is_same_v<Number, int> || std::is_same_v<Number, float>);
        assert(logicalMinimum <= travelMinimum && travelMinimum <= travelMaximum &&
            travelMaximum <= logicalMaximum);
        const ImGuiDataType type = std::is_same_v<Number, int>
            ? ImGuiDataType_S32 : ImGuiDataType_Float;
        char inputFormat[sizeof("%.1f centimeters")];
        if (!FormatSliderInput(inputFormat, format))
        {
            uvsr::log::error("The slider input format exceeds its checked control bound.");
            return false;
        }
        bool changed;
        const float width = ImGui::CalcItemWidth();
        const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
        const float numberWidth = ImGui::GetFrameHeight() * 3.5f;
        ImGui::PushID(label);
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(std::max(ImGui::GetFrameHeight() * 2.f,
            width - numberWidth - gap));
        changed = ImGui::SliderScalar("##slider", type, value,
            &travelMinimum, &travelMaximum, "",
            flags | ImGuiSliderFlags_NoInput | ImGuiSliderFlags_NoRoundToFormat |
                ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine(0.f, gap);
        ImGui::SetNextItemWidth(numberWidth);
        const bool edited = ImGui::InputScalar("##value", type, value,
            nullptr, nullptr, inputFormat);
        if (edited)
            *value = std::clamp(*value,
                m_ui.OverrideVisualMaxes ? logicalMinimum : travelMinimum,
                m_ui.OverrideVisualMaxes ? logicalMaximum : travelMaximum);
        changed |= edited;
        const char* labelEnd = ImGui::FindRenderedTextEnd(label);
        if (labelEnd != label)
        {
            ImGui::SameLine(0.f, gap);
            ImGui::TextUnformatted(label, labelEnd);
        }
        ImGui::EndGroup();
        ImGui::PopID();
        if (changed)
            *value = std::clamp(*value, logicalMinimum, logicalMaximum);
        return changed;
    }

    static bool DrawCenteredActionButton(const char* label, float width);

public:
    ~UIRenderer() override;
    UIRenderer(
        DeviceManager* deviceManager,
        UvsrSceneViewer* app,
        UIData& ui,
        std::string_view startupSettingsSnapshotCode) noexcept;

    void Animate(float elapsedTimeSeconds) override;

    // one attempt after viewer initialization succeeds; destroy this owner on failure.
    [[nodiscard]] bool Init(const uvsr::RendererNvrhiMessageCallback& messages, SettingsSnapshotError& error);
    [[nodiscard]] const char* RequiredFontFailure() const noexcept { return m_RequiredFontFailure; }
    void PacePresentation();
    bool ShouldAnimateUnfocused() override { return true; }
    bool SupportsDepthBuffer() override { return false; }

#if defined(UVSR_BUILD_TESTING)
    [[nodiscard]] bool ChangeRuntimeDiagnosticMaterial(
        SettingsSnapshotError& error);

    [[nodiscard]] bool ChangeRuntimeDiagnosticLight(
        SettingsSnapshotError& error);

    [[nodiscard]] bool SelectRuntimeDiagnosticFlashlight(
        SettingsSnapshotError& error);

    [[nodiscard]] bool ToggleRuntimeDiagnosticFlashlight(
        SettingsSnapshotError& error);

    void DriveRetainedRuntimeDiagnostic();
    int VerifyCanonicalSettingsContract();

#endif

    virtual void Render(nvrhi::IFramebuffer* framebuffer) override;

    virtual void BackBufferResizing() override;

    virtual void DisplayScaleChanged(
        float scaleX,
        float scaleY) override;

protected:
    virtual bool KeyboardUpdate(
        int key,
        int scancode,
        int action,
        int mods) override;

    bool KeyboardCharInput(unsigned int unicode, int mods) override;
    bool MousePosUpdate(double x, double y) override;
    bool MouseScrollUpdate(double x, double y) override;
    bool MouseButtonUpdate(int button, int action, int mods) override;
    void buildUI();
};
