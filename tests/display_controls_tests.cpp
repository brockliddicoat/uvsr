#include "fast_approximate_aa_options.h"
#include "fast_approximate_aa_contract.h"
#include "adaptive_sync.h"
#include "display_sync_test.h"
#include "auto_exposure.h"
#include "pixel_zoom.h"
#include "pixel_zoom_mapping.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
    bool Near(float a, float b) { return std::abs(a - b) < 1e-6f; }

    void CheckDisplaySyncTest()
    {
        using namespace uvsr;
        DisplaySyncTestSettings settings;
        settings.sweep = true;
        Require(Near(float(DisplaySyncTargetFps(settings, 0.0)), 65.f) &&
            Near(float(DisplaySyncTargetFps(settings, 6.0)), 117.f) &&
            Near(float(DisplaySyncTargetFps(settings, 12.0)), 65.f), "FPS sweep endpoints and period");
        for (int frameRate : { 30, 60, 144, 500 })
        {
            double position = 0.0;
            for (int frame = 0; frame < frameRate; ++frame)
                position = AdvanceDisplaySyncPosition(position, 1.0 / frameRate, 1200.0, false);
            Require(std::abs(position - 48.0) < 1e-8, "motion speed must not depend on FPS");
            Require(AdvanceDisplaySyncPosition(position, 1.0, 1200.0, true) == position, "paused pattern moved");
        }
        Require(std::abs(NextDisplaySyncDeadline(10.0, 10.002, 0.01) - 10.01) < 1e-9,
            "render time was added to the frame period");
        Require(std::abs(NextDisplaySyncDeadline(10.0, 10.040, 0.01) - 10.050) < 1e-9,
            "missed deadlines must not cause catch-up frames");
    }



    void CheckSettings()
    {
        using namespace uvsr;
        using Quality = AntiAliasingQuality;
        for (unsigned index = 0; index < 4; ++index)
        {
            const auto quality = static_cast<Quality>(index);
            FastApproximateAaSettings fxaa;
            ApplyFastApproximateAaQualityPreset(fxaa, quality);
            Require(MatchesFastApproximateAaQualityPreset(fxaa), "FXAA preset did not select its recipe");
            fxaa.edgeThreshold += .01f;
            Require(!MatchesFastApproximateAaQualityPreset(fxaa), "FXAA override did not become custom");
            ApplyFastApproximateAaQualityPreset(fxaa, quality);
            Require(MatchesFastApproximateAaQualityPreset(fxaa), "FXAA preset did not clear its override");
        }
        using Mode = AdaptiveSyncMode;
        struct Capability { uint32_t vendor; bool tearing; Mode defaultMode; unsigned mask; };
        for (const auto& capability : { Capability{ 0x10de, false, Mode::Off, 1 }, { 0x1002, false, Mode::Off, 1 },
                 { 0x8086, false, Mode::Off, 1 }, { 0x10de, true, Mode::NvidiaExclusive, 7 },
                 { 0x1002, true, Mode::VendorAgnostic, 3 }, { 0x8086, true, Mode::VendorAgnostic, 3 } })
        {
            Require(DefaultAdaptiveSyncMode(capability.vendor, capability.tearing) == capability.defaultMode,
                "adaptive sync default ignored adapter capability");
            for (unsigned mode = 0; mode < 3; ++mode)
                Require(IsAdaptiveSyncModeAvailable(static_cast<Mode>(mode), capability.vendor, capability.tearing) ==
                    bool(capability.mask & (1u << mode)), "adaptive sync exposed an unsupported mode");
        }
        Require(!AdaptiveSyncRequestsPresentTearing(Mode::Off) && AdaptiveSyncRequestsPresentTearing(Mode::VendorAgnostic) &&
            AdaptiveSyncRequestsPresentTearing(Mode::NvidiaExclusive), "adaptive sync selected wrong present flags");
    }

    void CheckExposure()
    {
        using namespace uvsr;
        AutoExposureSettings settings;
        for (float input : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -100.f, 100.f })
        {
            settings.exposureCompensationEV = settings.maximumBrighteningEV = settings.maximumDarkeningEV =
                settings.adjustmentPeriodSeconds = input;
            const auto bounded = SanitizeAutoExposureSettings(settings);
            Require(bounded.exposureCompensationEV >= -18 && bounded.exposureCompensationEV <= 8 &&
                bounded.maximumBrighteningEV >= 0 && bounded.maximumBrighteningEV <= 16 &&
                bounded.maximumDarkeningEV >= 0 && bounded.maximumDarkeningEV <= 16 &&
                bounded.adjustmentPeriodSeconds >= .05f && bounded.adjustmentPeriodSeconds <= 5,
                "exposure runtime sanitation did not restore finite, bounded controls");
        }
        for (unsigned unavailable = 0; unavailable < 4; ++unavailable)
        {
            settings.enabled = unavailable != 0;
            AutoExposureFrameHistory history{ false, true, true };
            const auto decision = BeginAutoExposureFrame(history, settings,
                unavailable == 1, unavailable != 2, unavailable != 3);
            Require(!decision.dispatch && !decision.resetExposure && history.resetRequested &&
                !history.wasEnabled && !history.exposureInitialized, "unavailable exposure retained stale history");
        }
        settings.enabled = true;
        AutoExposureFrameHistory history;
        auto decision = BeginAutoExposureFrame(history, settings, false, true, true);
        Require(decision.dispatch && decision.resetExposure && history.wasEnabled &&
            !history.exposureInitialized, "first exposure frame was not uncommitted");
        for (unsigned pass = 0; pass < 2; ++pass)
        {
            CompleteAutoExposureFrame(history, true);
            decision = BeginAutoExposureFrame(history, settings, false, true, true);
            Require(decision.dispatch && !decision.resetExposure, "committed exposure history did not continue");
            RequestAutoExposureReset(history);
            decision = BeginAutoExposureFrame(history, settings, false, true, true);
            Require(decision.dispatch && decision.resetExposure, "explicit exposure reset was lost");
        }
        CompleteAutoExposureFrame(history, false);
        Require(history.resetRequested && !history.wasEnabled && !history.exposureInitialized,
            "failed exposure dispatch published partial history");
        decision = BeginAutoExposureFrame(history, settings, false, true, true);
        Require(decision.dispatch && decision.resetExposure, "exposure recovery reused failed history");
    }

    void CheckPixelZoom()
    {
        using namespace uvsr;
        struct Mode { PixelZoomMode mode; unsigned factor; std::string_view area; };
        for (const Mode mode : { Mode{ PixelZoomMode::Zoom2x, 2, "4x" },
                Mode{ PixelZoomMode::Zoom3x, 3, "9x" }, Mode{ PixelZoomMode::Zoom4x, 4, "16x" },
                Mode{ PixelZoomMode::Zoom5x, 5, "25x" } })
        {
            Require(IsPixelZoomEnabled(mode.mode) && GetPixelZoomFactor(mode.mode) == mode.factor &&
                GetPixelZoomAreaLabel(mode.mode) == mode.area, "zoom control lost its exact factor or label");
            const auto layout = ResolvePixelZoomLayout(1902u, 1069u, 10u, mode.mode);
            Require(layout.panelMinX == 1359 && layout.panelMinY == 10 &&
                layout.panelWidth == 533 && layout.panelHeight == 300 && layout.zoomFactor == mode.factor,
                "zoom panel lost its margin or source aspect");
            for (const auto& axis : { std::pair{ layout.sourceWidth, layout.panelWidth },
                     std::pair{ layout.sourceHeight, layout.panelHeight } })
            {
                const int source = int(axis.first), panel = int(axis.second), factor = int(mode.factor);
                const auto coordinate = [&](int pixel) { return ResolvePixelZoomSourceCoordinate(source, panel, factor, pixel); };
                int centerPixels = 0;
                for (int pixel = 0; pixel < panel; ++pixel)
                {
                    const int selected = coordinate(pixel);
                    Require(selected >= 0 && selected < source, "zoom sampled outside the source image");
                    centerPixels += selected == source / 2;
                }
                const int first = (panel - factor) / 2;
                Require(centerPixels == factor && coordinate(first) == source / 2 &&
                    coordinate(first + factor - 1) == source / 2 && coordinate(first + factor) == source / 2 + 1,
                    "zoom did not preserve exact source texel groups");
            }
        }
        for (const auto& sample : { std::pair{ -100, 0 }, { -4, 3 }, { -3, 4 }, { -1, 4 }, { 0, 5 }, { 2, 5 }, { 3, 6 }, { 100, 10 } })
            Require(ResolvePixelZoomSourceCoordinate(11, 3, 3, sample.first) == sample.second,
                "shared pixel mapping lost negative floor division or clamping");
        Require(ResolvePixelZoomSourceCoordinate(11, 1, 4, -2) == 4 &&
            ResolvePixelZoomSourceCoordinate(11, 1, 4, 2) == 5, "tiny panels lost their signed group origin");
        const auto even = ResolveCenterMaterialPick(1920, 1080), odd = ResolveCenterMaterialPick(1901, 1069);
        Require(even.valid && even.x == 960 && even.y == 540 && odd.valid && odd.x == 950 && odd.y == 534 &&
            !ResolveCenterMaterialPick(0, 1080).valid && !ResolveCenterMaterialPick(1920, 0).valid &&
            !IsPixelZoomEnabled(PixelZoomMode::Off) && GetPixelZoomFactor(PixelZoomMode::Off) == 0 &&
            std::string_view(GetPixelZoomAreaLabel(PixelZoomMode::Off)).empty(), "zoom Off or center picking changed");
    }

    void CheckFxaaInputs()
    {
        using namespace uvsr;
        nvrhi::TextureDesc source;
        source.width = 1920; source.height = 1080;
        source.format = nvrhi::Format::RGBA16_FLOAT;
        Require(IsFastApproximateAaSourceCompatible(source, 1920, 1080, true) &&
            !IsFastApproximateAaSourceCompatible(source, 1920, 1080, false),
            "FXAA source aliasing contract changed");
        for (unsigned defect = 0; defect < 5; ++defect)
        {
            auto invalid = source;
            switch (defect)
            {
            case 0: ++invalid.sampleCount; break;
            case 1: invalid.dimension = nvrhi::TextureDimension::Texture2DArray; break;
            case 2: invalid.format = nvrhi::Format::R16_FLOAT; break;
            case 3: --invalid.width; break;
            case 4: --invalid.height; break;
            }
            Require(!IsFastApproximateAaSourceCompatible(invalid, 1920, 1080, true),
                "FXAA accepted incompatible source topology");
        }
        const FastApproximateAaViewContract complete{ 1, true, 0, 1, 0, 1, 0, 0, 1920, 1080 };
        const FastApproximateAaViewContract invalid[] = {
            { 2, true, 0, 1, 0, 1, 0, 0, 1920, 1080 },
            { 1, true, 0, 2, 0, 1, 0, 0, 1920, 1080 },
            { 1, true, 0, 1, 0, 1, 1, 0, 1920, 1080 }
        };
        Require(IsFastApproximateAaFullImageView(complete, 1920, 1080), "FXAA rejected full image view");
        for (const auto& view : invalid)
            Require(!IsFastApproximateAaFullImageView(view, 1920, 1080),
                "FXAA accepted a partial or multiple image view");
    }
}

int main()
{
    CheckSettings();
    CheckDisplaySyncTest();
    CheckFxaaInputs();
    CheckExposure();
    CheckPixelZoom();
    return EXIT_SUCCESS;
}
