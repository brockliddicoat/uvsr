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
#include "renderer_common_passes.h"
#include "renderer_shader_factory.h"
#include "renderer_statistics.h"
#include "renderer_gpu_contract.h"
#include "build_identity.h"
#include "windows_executable_path.h"
#include "scene_catalog.h"
#include "scene_loading.h"
#include "settings_snapshot_decoder.h"
#include "settings_snapshot.h"
#include "settings_snapshot_transaction.h"
#include "ui_layout.h"
#include "ui_settings_command_catalog.h"
#include "ui_performance_timing_rows.h"
#include "path_tracing_pass.h"
#include "renderer_log.h"
#include "display_sync_test.h"
#if defined(UVSR_BUILD_TESTING)
#include "retained_runtime_diagnostic.h"
#endif
#include <donut/engine/Scene.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/View.h>
#include <donut/app/imgui_renderer.h>
#include <donut/app/UserInterfaceUtils.h>
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
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <type_traits>
#include <initializer_list>

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace uvsr;

namespace uvsr_detail
{
    struct alignas(16) PixelZoomConstants
    {
        uint2 sourceSize;
        uint2 panelMin;

        uint2 panelSize;
        uint32_t zoomFactor = 0u;
        float cornerRadius = 8.f;

        float opacity = 0.f;
        float outlineWidth = 1.5f;
        float shadowBlur = 10.f;
        float shadowOpacity = 0.34f;

        float shadowOffsetY = 3.f;
        float3 padding;

        float4 outlineTopColor;
        float4 outlineBottomColor;
    };

    static_assert(sizeof(PixelZoomConstants) == 96u);

    class PixelZoomPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<uvsr::RendererCommonPasses> m_CommonPasses;
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
            const std::shared_ptr<uvsr::RendererShaderFactory>& shaderFactory,
            std::shared_ptr<uvsr::RendererCommonPasses> commonPasses)
            : m_Device(device)
            , m_CommonPasses(std::move(commonPasses))
        {
            m_CommandList = device->createCommandList();
            m_PixelShader = shaderFactory->CreateShader(
                "uvsr/pixel_zoom_ps.hlsl",
                "main",
                nullptr,
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
            constants.sourceSize = uint2(
                layout.sourceWidth,
                layout.sourceHeight);
            constants.panelMin = uint2(
                layout.panelMinX,
                layout.panelMinY);
            constants.panelSize = uint2(
                layout.panelWidth,
                layout.panelHeight);
            constants.zoomFactor = layout.zoomFactor;
            constants.cornerRadius = cornerRadius;
            constants.opacity = 1.f;
            // A centered one-pixel ImGui stroke fully covers its edge texels.
            // The 1.5-pixel signed-distance band reproduces that visual weight
            // without filtering the magnified interior.
            constants.outlineWidth = 1.5f;
            constants.outlineTopColor =
                float4(0.88f, 0.90f, 0.94f, 0.10f);
            constants.outlineBottomColor =
                float4(0.96f, 0.97f, 1.00f, 0.30f);

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

class RequiredUiFontStartupError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class UIRenderer : public ImGui_Renderer
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
        std::string display;
        bool truncated = false;
    };

    [[nodiscard]] static FrontEllipsisText FormatFrontEllipsisUtf8(
        std::string_view source,
        size_t maximumCodePoints);

    std::shared_ptr<UvsrSceneViewer> m_app;

    std::shared_ptr<app::RegisteredFont> m_UiBodyFont;
    std::shared_ptr<app::RegisteredFont> m_UiHeaderFont;
    bool m_RequiredFontsReady = false;
    std::shared_ptr<engine::Light> m_SelectedLight;
    double m_DisplayedFrameTime = 0.0;
    double m_StatSnapshotElapsed = 0.0;
    double m_StatFrameTimeSum = 0.0;
    uint32_t m_StatFrameTimeCount = 0;
    std::array<std::string, 4> m_PerformanceStatValues;
    uvsr::PerformanceTimingRowRetention m_PerformanceTimingRows;
    bool m_HasAppliedStatSnapshot = false;
    bool m_WasSceneLoading = false;
    bool m_SceneLoadFailed = false;
    std::chrono::steady_clock::time_point m_SceneLoadCounterStart;
    std::string m_SceneLoadHistoryKey;
    SceneLoadTimingDatabase m_SceneLoadTiming;

    [[nodiscard]] static std::filesystem::path
        GetSceneLoadTimingDatabasePath();

    void LoadSceneLoadTimingDatabase();

    void SaveSceneLoadTimingDatabase() const;
    std::unique_ptr<PixelZoomPass> m_PixelZoomPass;
    std::unordered_map<nvrhi::IFramebuffer*, nvrhi::FramebufferHandle>
        m_UiFramebuffers;
    nvrhi::Format m_UiFramebufferFormat = nvrhi::Format::UNKNOWN;
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
    void PacePresentation();
    void DrawDeveloperDrawer(float controlWidth);
    void DrawPostprocessDrawer(float controlWidth);
    float m_SettingsScrollY = 0.f;
    int m_SettingsScrollFrame = -1;
    SettingsSnapshotController m_SettingsSnapshots;
    std::string m_StartupSettingsSnapshotCode;
    bool m_StartupSettingsSnapshotAttempted = false;
    std::string m_PendingSettingsSnapshotCode;
#if defined(UVSR_BUILD_TESTING)
    bool SelectRuntimeDiagnostic(SettingId id, const std::string& selector,
        const char* emptyError, const char* failurePrefix, std::string& error);
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

    struct LightDefaultState
    {
        int type = UVSR_LIGHT_TYPE_NONE;
        double3 direction = double3(0.0, -1.0, 0.0);
        float3 color = float3(1.f);
        float irradiance = 1.f;
        float angularSize = 0.f;
        float radius = 0.f;
        float intensity = 1.f;
        float innerAngle = 180.f;
        float outerAngle = 180.f;
    };

    std::unordered_map<
        std::string,
        LightDefaultState> m_LightDefaults;

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
    inline static std::vector<NestedDrawerContext> g_NestedDrawerContexts;

    static void BeginControlRegion(ImGuiID id);
    static void EndControlRegion();
    static bool BeginSettingsTree(const char* label,
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None, const char* tooltip = nullptr);
    static void EndSettingsTree();
    static bool BeginToggleRegion(const char* id, bool visible);

    static void DrawMaterialEditorTextureFilename(
        const char* filename,
        const float4& color);

    static void SetNextLabeledControlWidth(
        const char* label,
        float preferredWidth);

    static bool DrawPresetResetIcon(const char* id, bool modified,
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

    static std::filesystem::path GetWindowsFontsDirectory();

    bool IsCommandRuntimeMutationLocked(
        const UiSettingsCommandDefinition& definition) const;

    bool CheckCommandMutationAllowed(
        const UiSettingsCommandDefinition& definition,
        std::string& error) const;

    void RequestMaterialDrawerVisible(bool visible);

    const GpuAdapterChoice* GetActiveGpuAdapterChoice() const;

    void ApplyLightingSolution(
        LightingSolution solution,
        bool invalidateHistory = true);

    [[nodiscard]] bool ResetAllSettingsToFactoryDefaults(
        std::string& error);

    [[nodiscard]] bool RunAction(
        ActionId id,
        std::string& error);

    std::shared_ptr<Light> GetDefaultCommandLight() const;

    std::shared_ptr<Light> EnsureCommandSelectedLight();

    const LightDefaultState& GetCommandLightDefaults(
        const std::shared_ptr<Light>& light);

    static std::pair<float, float> GetCommandLightAngles(
        const double3& storedDirection,
        bool directional);

    static double3 MakeCommandLightDirection(
        float azimuthDegrees,
        float elevationDegrees,
        bool directional);

    static bool IsCommandMaterialTransmissive(MaterialDomain domain);

    static bool IsCommandMaterialAlphaTested(MaterialDomain domain);

    static bool IsCommandMaterialAlphaBlended(MaterialDomain domain);

    bool DispatchTypedSetting(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue* requested,
        UiSettingsValue& value,
        std::string& error,
        bool allowLatentMutation = false,
        bool deferMutationEffects = false);

    [[nodiscard]] bool ResolveSettingDefaultValue(
        const UiSettingsCommandDefinition& definition,
        UiSettingsValue& value,
        std::string& error);

    void ApplySettingMutationEffects(
        const UiSettingsCommandDefinition& definition);

    [[nodiscard]] bool ReadSettingValue(
        SettingId id,
        std::string& value,
        std::string& error);

    [[nodiscard]] bool ReadSettingValue(
        SettingId id,
        UiSettingsValue& value,
        std::string& error);

    [[nodiscard]] bool ApplySettingValue(
        SettingId id,
        std::string_view canonicalValue,
        std::string& error);

    [[nodiscard]] bool ApplySettingValue(
        SettingId id,
        const UiSettingsValue& value,
        std::string& error,
        bool deferMutationEffects = false);

    [[nodiscard]] bool ResetSettingValue(
        SettingId id,
        std::string& error);

    [[nodiscard]] bool IsSettingAvailable(SettingId id) const;

    [[nodiscard]] bool IsSettingAtContextualDefault(
        SettingId id,
        std::string& error);

    [[nodiscard]] SettingsSnapshotRuntimeAccess
    MakeSettingsSnapshotRuntimeAccess();

    void RefreshSettingsSnapshot();

    void CopySettingsSnapshot();

    void FailStartupSettingsSnapshot(
        std::string_view code,
        std::string_view error);

    void HandleStagedSettingsSnapshotStep(
        const SettingsSnapshotTransactionStep& step);

    void TryApplyStartupSettingsSnapshot();

    void DrawPerformancePanelContents(
        float settingsControlWidth,
        const std::string& performanceLine);

    UiSettingsValue ReadUiPresentationValue(SettingId id);
    bool ApplyUiSetting(SettingId id, const UiSettingsValue& value);
    bool ResetUiSetting(SettingId id);
    bool IsUiSettingChanged(SettingId id);
    std::size_t ReadUiTokenIndex(SettingId id);
    bool DrawUiReset(SettingId id, bool nested = false, const char* resetId = nullptr,
        const char* tooltip = "Reset this setting to its default value.");
    void DrawUiBoolean(const char* label, SettingId id, const char* tooltip,
        bool nestedReset = false, const std::string* texturePath = nullptr, const char* resetId = nullptr);
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
    static void FormatStatLine(
        std::string& destination,
        const char* format,
        Arguments... arguments)
    {
        char buffer[512];
        snprintf(
            buffer,
            std::size(buffer),
            format,
            arguments...);
        destination = buffer;
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
        std::string inputFormat = format;
        for (const auto& [word, unit] : { std::pair{ " degrees", "\xC2\xB0" },
            std::pair{ " centimeters", " cm" }, std::pair{ " candela", " cd" } })
        {
            const auto offset = inputFormat.find(word);
            if (offset != std::string::npos)
                inputFormat.replace(offset, std::strlen(word), unit);
        }
        const bool edited = ImGui::InputScalar("##value", type, value,
            nullptr, nullptr, inputFormat.c_str());
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
        std::shared_ptr<UvsrSceneViewer> app,
        UIData& ui,
        std::string startupSettingsSnapshotCode);

    void Animate(float elapsedTimeSeconds) override;

    bool Init(std::shared_ptr<ShaderFactory> shaderFactory);

#if defined(UVSR_BUILD_TESTING)
    [[nodiscard]] bool ChangeRuntimeDiagnosticMaterial(
        std::string& error);

    [[nodiscard]] bool ChangeRuntimeDiagnosticLight(
        std::string& error);

    [[nodiscard]] bool SelectRuntimeDiagnosticFlashlight(
        std::string& error);

    [[nodiscard]] bool ToggleRuntimeDiagnosticFlashlight(
        std::string& error);

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

    virtual void buildUI(void) override;
};
