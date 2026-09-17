#include "settings_snapshot_legacy.h"
#include "settings_value.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

using namespace uvsr;

namespace
{
    unsigned checks = 0;
    void Require(bool accepted, const char* reason)
    {
        ++checks;
        if (!accepted) { std::fprintf(stderr, "%s\n", reason); std::exit(1); }
    }

    void Set(DecodedSettings& candidate, std::string_view name, std::string_view value)
    {
        SettingsSnapshotError error;
        Require(candidate.Set(name, value, error), "cannot prepare legacy fixture");
    }

    DecodedSettings Palette()
    {
        DecodedSettings candidate;
        for (const auto entry : {
            DecodedSetting{"ui.skin", "amp"},
            {"ui.accent.primary", "0 0.5 1 1"},
            {"ui.accent.font", "0 0.5 1 1"},
            {"ui.accent.primary-background", "0 0.5 1 1"},
            {"ui.accent.secondary", "0 0.5 1 1"},
            {"ui.accent.tertiary", "0 0.5 1 1"},
            {"ui.font-family", "codex"}})
            Set(candidate, entry.name, entry.value);
        Set(candidate, "fixture.retained", {"a\0z", 3});
        return candidate;
    }

    DecodedSettings Visibility()
    {
        auto candidate = Palette();
        for (const auto entry : {
            DecodedSetting{"visibility.enabled", "on"},
            {"visibility.quality", "low"},
            {"visibility.estimator", "projected-angle"},
            {"visibility.resolution", "full"},
            {"visibility.samples", "1"},
            {"visibility.radius", "1"},
            {"visibility.thickness", "0.5"},
            {"visibility.distribution", "0.25"},
            {"visibility.specify-noise", "on"},
            {"visibility.noise-pattern", "spatial-white"},
            {"visibility.noise-resolution", "64x64"},
            {"visibility.animate-samples", "on"},
            {"visibility.ao.enabled", "on"},
            {"visibility.ao.strength", "0"},
            {"visibility.ao.precision", "16-bit"},
            {"visibility.gi.enabled", "on"},
            {"visibility.gi.intensity", "0"},
            {"visibility.gi.precision", "16-bit"},
            {"debug.visibility.view", "final"}})
            Set(candidate, entry.name, entry.value);
        return candidate;
    }

    void PartialFailureAndDiagnostics()
    {
        SettingsSnapshotError saved;
        {
            auto candidate = Palette();
            Set(candidate, "ui.font-family", "invalid");
            SettingsSnapshotError error;
            Require(!ApplyLegacySettingMigrations(23, candidate, error), "late legacy invalid accepted");
            Require(error.code == SettingsSnapshotErrorCode::InvalidInput &&
                error.MessageView() == "schema 23 snapshot has invalid retired setting 'ui.font-family'",
                "legacy diagnostic changed");
            Require(candidate.Count() == 2 && candidate.Find("ui.font-family") &&
                candidate.Find("fixture.retained")->value == std::string_view("a\0z", 3),
                "late failure changed partial candidate order or retained bytes");
            saved = std::move(error);
        }
        Require(saved.MessageView() == "schema 23 snapshot has invalid retired setting 'ui.font-family'",
            "legacy diagnostic borrowed candidate storage");

        auto candidate = Palette();
        Set(candidate, "ui.font-family", "invalid");
        SettingsSnapshotError error;
        json::FailAllocationAfter(0);
        const bool accepted = ApplyLegacySettingMigrations(23, candidate, error);
        json::ClearAllocationFailure();
        Require(!accepted && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            !error.MessageView().empty() && candidate.Count() == 2 && candidate.Find("ui.font-family"),
            "diagnostic OOM changed migration order or lost its failure");
        candidate = Palette();
        Require(ApplyLegacySettingMigrations(23, candidate, error) && error.code == SettingsSnapshotErrorCode::None &&
            error.MessageView().empty(), "successful legacy retry retained stale error");
    }

    void ColorAndNumberOwnership()
    {
        const auto denorm = FormatUiSettingsMetadataFloat(std::numeric_limits<float>::denorm_min());
        char color[96];
        const int length = std::snprintf(color, sizeof(color), "%.*s 0 0 1",
            static_cast<int>(denorm.View().size()), denorm.View().data());
        Require(length > 0 && static_cast<size_t>(length) < sizeof(color), "cannot prepare canonical subnormal");
        auto candidate = Palette();
        Set(candidate, "ui.accent.primary", {color, static_cast<size_t>(length)});
        SettingsSnapshotError error;
        Require(ApplyLegacySettingMigrations(23, candidate, error), "canonical subnormal color was rejected");

        candidate = Palette();
        Set(candidate, "ui.accent.primary", "0.0000000000000000000000000000000000 0 0 1");
        FailUiSettingsValueAllocationAfter(0);
        const bool accepted = ApplyLegacySettingMigrations(23, candidate, error);
        ClearUiSettingsValueAllocationFailure();
        Require(!accepted && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            !candidate.Find("ui.skin") && candidate.Find("ui.accent.primary") && candidate.Count() == 7,
            "color scratch OOM was flattened or changed candidate order");

        candidate = Visibility();
        Set(candidate, "visibility.ao.strength", "0.0000000000000000000000000000000000");
        FailUiSettingsValueAllocationAfter(0);
        const bool numericAccepted = ApplyLegacySettingMigrations(20, candidate, error);
        ClearUiSettingsValueAllocationFailure();
        Require(!numericAccepted && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            candidate.Find("visibility.ao.strength") && !candidate.Find("visibility.ao.enabled"),
            "number scratch OOM was flattened or changed candidate order");
    }

    void DefaultInsertionFailures()
    {
        // version 6 directly exercises defaults without the supported-load retired rows.
        DecodedSettings control;
        SettingsSnapshotError error;
        Require(ApplyLegacySettingMigrations(6, control, error) && control.Count() == 15,
            "default migration control changed");
        bool textFailure = false;
        bool entriesFailure = false;
        bool success = false;
        for (size_t budget = 0; budget < 64; ++budget)
        {
            DecodedSettings candidate;
            FailSettingsSnapshotAllocationAfter(budget);
            const bool accepted = ApplyLegacySettingMigrations(6, candidate, error);
            ClearSettingsSnapshotAllocationFailure();
            for (size_t i = 0; i < candidate.Count(); ++i)
            {
                const auto& entry = candidate.Entries()[i];
                const auto* expected = control.Find(entry.name);
                Require(expected && expected->value == entry.value, "default failure published an incomplete value");
            }
            if (accepted)
            {
                Require(candidate.Count() == control.Count() && error.code == SettingsSnapshotErrorCode::None,
                    "default retry missed fields or retained error");
                success = true;
                break;
            }
            Require(error.code == SettingsSnapshotErrorCode::OutOfMemory && candidate.Count() < control.Count(),
                "default insertion OOM was flattened or success was reported");
            textFailure = textFailure || error.MessageView() == "cannot allocate snapshot text";
            entriesFailure = entriesFailure || error.MessageView() == "cannot allocate snapshot entries";
        }
        Require(success && textFailure && entriesFailure, "default fault sweep did not cover text and entry growth");

        DecodedSettings candidate;
        json::FailAllocationAfter(0);
        FailUiSettingsValueAllocationAfter(0);
        const bool accepted = ApplyLegacySettingMigrations(6, candidate, error);
        ClearUiSettingsValueAllocationFailure();
        json::ClearAllocationFailure();
        Require(accepted && candidate.Count() == 15, "short typed defaults allocated intermediate text");

        candidate = Palette();
        Set(candidate, "pathing.minimum-bounces", "1");
        json::FailAllocationAfter(0);
        const bool duplicateAccepted = ApplyLegacySettingMigrations(21, candidate, error);
        json::ClearAllocationFailure();
        Require(!duplicateAccepted && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            candidate.Find("pathing.maximum-bounces") && candidate.Find("pathing.minimum-bounces") &&
            !candidate.Find("pathing.firefly-filter") && !candidate.Find("ui.skin"),
            "default diagnostic OOM changed insertion order");
    }
}

int main()
{
    PartialFailureAndDiagnostics();
    ColorAndNumberOwnership();
    DefaultInsertionFailures();
    std::printf("legacy migration ownership checks: %u passed\n", checks);
    return 0;
}
