#include "fast_approximate_aa_options.h"
#include "fast_approximate_aa_contract.h"
#include "display_sync_test.h"
#include "auto_exposure.h"
#include "pixel_zoom.h"
#include "pixel_zoom_mapping.h"
#include "tone_mapping_settings.h"
#include "color_lut.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>
#include <fstream>
#include <filesystem>
#include <sstream>

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

    void CheckToneMapping(const std::filesystem::path& directory)
    {
        using namespace uvsr;
        ToneMappingSettings settings;
        Require(!settings.enabled, "the tonemapper must default off");
        Require(FindToneMappingPreset(settings) == 0, "neutral tone controls must select Base");
        settings.lut = ToneMappingLut::Portra400;
        Require(FindToneMappingPreset(settings) == 0, "film selection must not change the grade preset");
        settings.contrast = 1.1f;
        Require(FindToneMappingPreset(settings) == -1, "edited tone controls must display Custom");
        for (size_t index = 0; index < ToneMappingPresets.size(); ++index)
            Require(FindToneMappingPreset(ToneMappingPresets[index].settings) == int(index),
                "each retained grade must remain selectable");
        for (ToneMappingLut lut : { ToneMappingLut::Print2383, ToneMappingLut::Portra400, ToneMappingLut::Ektar100 })
        {
            std::ifstream stream(directory / ToneMappingLutFilename(lut));
            ColorLutData data;
            std::string error;
            Require(ReadColorLut(stream, data, error) && data.size == 17 && data.values.size() == 4913,
                "each bundled film LUT must contain its complete 17-cubed table");
        }
        const std::string identity = "LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
        ColorLutData retained;
        std::string error;
        std::istringstream valid(identity);
        Require(ReadColorLut(valid, retained, error) && retained.values[1][0] == 1.f &&
                retained.values[2][1] == 1.f && retained.values[4][2] == 1.f,
            "cube order must remain red-fastest for the GPU texture");
        for (const std::string& invalid : { std::string("LUT_3D_SIZE 129\n"),
                std::string("LUT_3D_SIZE 2\n0 0 0\n"), identity + "0 0 0\n",
                std::string("DOMAIN_MAX 0 1 1\n") + identity,
                std::string("DOMAIN_MIN nan 0 0\n") + identity,
                std::string("LUT_1D_SIZE 2\n") + identity,
                std::string("LUT_3D_SIZE 2\n") + identity })
        {
            std::istringstream stream(invalid);
            Require(!ReadColorLut(stream, retained, error) && !error.empty() &&
                    retained.size == 2 && retained.values.size() == 8 && retained.values[7][2] == 1.f,
                "rejected LUT data must leave the previous complete table unchanged");
        }
    }

    void CheckDisplaySyncTest()
    {
        using namespace uvsr;
        DisplaySyncTestSettings settings;
        DisplayPresentationSettings presentation;
        Require(!presentation.frameRateLimitEnabled && presentation.frameRateLimit > 0 &&
                DisplayPresentationTargetFps(presentation, false, 0.0) == 0.0,
            "the limiter must default off while retaining a positive rate");
        presentation.frameRateLimitEnabled = true;
        presentation.frameRateLimit = 60;
        Require(DisplaySyncTargetFps(settings, 0.0, 120.0) == 0.0 &&
                DisplayPresentationTargetFps(presentation, true, 0.0) == 60.0 &&
                DisplayPresentationTargetFps(presentation, false, 0.0) == 60.0,
            "default test and scene must use the same frame limit");
        Require(DisplayPresentationTargetFps(presentation, true, 120.0) == 60.0 &&
                DisplayPresentationTargetFps(presentation, true, 30.0) == 30.0 &&
                DisplayPresentationTargetFps(presentation, false, 30.0) == 60.0,
            "test targets must respect the shared ceiling without limiting the scene after the test stops");
        presentation.frameRateLimitEnabled = false;
        Require(DisplayPresentationTargetFps(presentation, false, 30.0) == 0.0 &&
                DisplayPresentationTargetFps(presentation, true, 30.0) == 30.0,
            "the toggle must disable the shared limit while preserving explicit test targets");
        Require(presentation.frameRateLimit == 60, "disabling the limiter must retain its rate");
        DisplayPresentationSettings previous = presentation;
        presentation.verticalSynchronization = true;
        ReconcileDisplayPresentation(presentation, previous, 240.0, 240.0);
        Require(presentation.frameRateLimitEnabled && presentation.frameRateLimit == 240,
            "enabling VSync must enable the limiter at the panel refresh rate");
        previous = presentation;
        presentation.frameRateLimit = 60;
        ReconcileDisplayPresentation(presentation, previous, 240.0, 240.0);
        Require(presentation.frameRateLimit == 60,
            "a lower limit selected after enabling VSync must remain effective");
        previous = presentation;
        ReconcileDisplayPresentation(presentation, previous, 144.0, 240.0);
        Require(presentation.frameRateLimit == 60, "a display change must retain a lower user limit");
        presentation.frameRateLimit = 144;
        previous = presentation;
        ReconcileDisplayPresentation(presentation, previous, 240.0, 144.0);
        Require(presentation.frameRateLimit == 240, "a limit at the old refresh rate must follow the new display");
        previous = presentation;
        presentation.verticalSynchronization = false;
        ReconcileDisplayPresentation(presentation, previous, 240.0, 240.0);
        Require(!presentation.frameRateLimitEnabled && presentation.frameRateLimit == 240 &&
                DisplayPresentationTargetFps(presentation, false, 0.0) == 0.0,
            "disabling VSync must disable the limiter while retaining its stored rate");
        previous = presentation;
        presentation.frameRateLimitEnabled = true;
        ReconcileDisplayPresentation(presentation, previous, 240.0, 240.0);
        Require(presentation.frameRateLimitEnabled,
            "the limiter must still be independently usable while VSync stays off");
        previous = presentation;
        presentation.verticalSynchronization = true;
        ReconcileDisplayPresentation(presentation, previous, 0.0, 240.0);
        Require(presentation.frameRateLimit == 240, "an unknown display refresh must retain the stored limit");
        presentation.frameRateLimitEnabled = true;
        presentation.frameRateLimit = 960;
        Require(DisplayPresentationTargetFps(presentation, false, 0.0) == 960.0,
            "the requested maximum frame limit must be reachable");
        settings.rateMode = DisplaySyncRateMode::BelowRefresh;
        for (double refresh : { 24.0, 30.0, 60.0, 120.0, 144.0, 240.0, 360.0, 500.0, 960.0 })
        {
            const double target = DisplaySyncTargetFps(settings, 0.0, refresh);
            Require(target < refresh && target >= refresh * 0.85,
                "default test target must stay below refresh without unnecessarily entering the low FPS range");
        }
        settings.rateMode = DisplaySyncRateMode::AboveRefresh;
        Require(DisplaySyncTargetFps(settings, 0.0, 120.0) > 120.0, "tearing reference target is not above refresh");
        settings.rateMode = DisplaySyncRateMode::Fixed;
        settings.targetFps = 91.f;
        Require(DisplaySyncTargetFps(settings, 0.0, 120.0) == 91.0, "fixed FPS target was overridden");
        settings.rateMode = DisplaySyncRateMode::BelowRefresh;
        Require(DisplaySyncTargetFps(settings, 0.0, 0.0) == 91.0, "unknown refresh must allow the explicit FPS target");
        settings.rateMode = DisplaySyncRateMode::Sweep;
        Require(Near(float(DisplaySyncTargetFps(settings, 0.0, 120.0)), 65.f) &&
            Near(float(DisplaySyncTargetFps(settings, 6.0, 120.0)), 117.f) &&
            Near(float(DisplaySyncTargetFps(settings, 12.0, 120.0)), 65.f), "FPS sweep endpoints and period");
        for (int frameRate : { 30, 60, 144, 500 })
        {
            double position = 0.0;
            for (int frame = 0; frame < frameRate; ++frame)
                position = AdvanceDisplaySyncPosition(position, 1.0 / frameRate, 1200.0, false);
            Require(std::abs(position - 48.0) < 1e-8, "motion speed must not depend on FPS");
            Require(AdvanceDisplaySyncPosition(position, 1.0, 1200.0, true) == position, "paused pattern moved");
        }
        Require(CanPresentWithoutSynchronization(false, true, true) &&
                !CanPresentWithoutSynchronization(true, true, true) &&
                !CanPresentWithoutSynchronization(false, false, true) &&
                !CanPresentWithoutSynchronization(false, true, false),
            "only supported unsynchronized windowed presentation may allow tearing");
        for (bool vsync : { false, true })
        {
            for (int limit : { 0, 60, 90, 120, 240, 960 })
            {
                presentation = { vsync, limit != 0, std::max(limit, 1) };
                Require(DisplayPresentationTargetFps(presentation, false, 0.0) == limit,
                    "Vertical Sync must not discard the requested software frame limit");
                const double effective = EffectivePresentationCeiling(limit, vsync, 120.0);
                const double expected = vsync ? (limit > 0 ? std::min(limit, 120) : 120) : limit;
                Require(effective == expected,
                    "the reported ceiling must include both the user limit and synchronized display refresh");
            }
        }
        Require(EffectivePresentationCeiling(240.0, true, 0.0) == 240.0 &&
                EffectivePresentationCeiling(0.0, true, 0.0) == 0.0,
            "unavailable display information must not invent a refresh ceiling");
        Require(PresentationFrameRateMaximum(true, 240.0) == 240 &&
                PresentationFrameRateMaximum(true, 60.0) == 60 &&
                PresentationFrameRateMaximum(false, 240.0) == 960 &&
                PresentationFrameRateMaximum(true, 0.0) == 960,
            "VSync must constrain the editable maximum to the known active refresh rate");
        Require(std::abs(NextPresentationDeadline(10.002, 100.0) - 10.012) < 1e-9,
            "a slightly late frame must not shorten the next interval to catch up");
        Require(std::abs(NextPresentationDeadline(10.040, 100.0) - 10.050) < 1e-9,
            "missed deadlines must not cause catch-up frames");
        Require(NextPresentationDeadline(10.002, 0.0) == 0.0 &&
                NextPresentationDeadline(10.0, 1.0) == 11.0 &&
                std::abs(NextPresentationDeadline(10.0, 960.0) - (10.0 + 1.0 / 960.0)) < 1e-9,
            "disabled, minimum, and maximum limits must produce valid deadlines");
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

int main(int argc, char** argv)
{
    Require(argc == 2, "expected bundled LUT directory");
    CheckToneMapping(argv[1]);
    CheckSettings();
    CheckDisplaySyncTest();
    CheckFxaaInputs();
    CheckExposure();
    CheckPixelZoom();
    return EXIT_SUCCESS;
}
