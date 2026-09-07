#include "settings_snapshot_schema.h"
#include "settings_snapshot_transaction.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace uvsr;
    using Id = SettingId;
    using Stage = SettingsSnapshotTransactionFailureStage;
    using Values = std::map<Id, std::string>;
    using Requests = std::vector<SettingsSnapshotTransactionEntry>;

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "Settings snapshot values failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    const UiSettingsCommandDefinition& Definition(Id id)
    {
        const auto found = std::find_if(UiSettingsCommandCatalog.begin(),
            UiSettingsCommandCatalog.end(), [id](const auto& item) { return item.id == id; });
        Require(found != UiSettingsCommandCatalog.end(), "unknown fixture ID");
        return *found;
    }

    Requests Request(const Values& values)
    {
        Requests result;
        for (const auto& [id, value] : values)
            result.push_back({ id, value });
        return result;
    }

    SettingsSnapshotTransactionResult Run(
        const Requests& request, const SettingsSnapshotStagedRuntimeAccess& access)
    {
        SettingsSnapshotTransactionCoordinator coordinator;
        auto step = coordinator.Begin(request, access);
        for (unsigned iteration = 0u;
             step.progress == SettingsSnapshotTransactionProgress::Pending && iteration < 64u;
             ++iteration)
            step = coordinator.Advance(access);
        Require(!coordinator.IsActive(), "synchronous fixture did not terminate");
        return step.result;
    }

    enum class Fault { None, BeforeWrite, AfterWrite, IgnoreWrite, Rollback };

    struct Runtime
    {
        Values values;
        SettingsSnapshotValidationContext context;
        Fault fault = Fault::None;
        Id rejectValue = Id::Invalid;
        Id rejectRead = Id::Invalid;
        unsigned reads = 0u;
        unsigned writes = 0u;
        unsigned validations = 0u;
        bool faultIssued = false;

        explicit Runtime(Values initial = {}) : values(std::move(initial)) {}

        SettingsSnapshotStagedRuntimeAccess Access()
        {
            const auto reader = [this](Id id, std::string& value, std::string& error) {
                ++reads;
                const auto found = values.find(id);
                if (id == rejectRead || found == values.end())
                {
                    error = "injected capture failure";
                    return false;
                }
                value = found->second;
                return true;
            };
            return {
                [this](Id id, std::string_view value, std::string_view, std::string& error) {
                    ++validations;
                    if (id == rejectValue)
                    {
                        error = "injected late parse failure";
                        return false;
                    }
                    return ValidateSettingsSnapshotCatalogValue(Definition(id), value, error, context);
                },
                reader, reader,
                [this](Id id, std::string_view value, std::string& error) {
                    ++writes;
                    if (fault != Fault::None && !faultIssued &&
                        id == Id::ShadowsRayTracedSamplesPerPixel && value == "32")
                    {
                        faultIssued = true;
                        if (fault == Fault::IgnoreWrite)
                            return true;
                        if (fault == Fault::AfterWrite)
                            values[id] = value;
                        error = "injected SET failure";
                        return false;
                    }
                    if (fault == Fault::Rollback && faultIssued &&
                        id == Id::NoisePattern && value == "spatiotemporal-blue")
                    {
                        error = "injected rollback failure";
                        return false;
                    }
                    values[id] = value;
                    return true;
                }, {}
            };
        }

        SettingsSnapshotTransactionResult Apply(const Requests& request)
        {
            return Run(request, Access());
        }
    };

    void TestCanonicalValues()
    {
        struct ValueCase { Id id; const char* valid; const char* invalid; };
        const ValueCase cases[] = {
            { Id::SkyAmbientFillEnabled, "on", "true" },
            { Id::LightingSolution, "path-tracing", "Path Tracing" },

            { Id::ShadowsRayTracedSamplesPerPixel, "64", "064" },
            { Id::ShadowsRayTracedSamplesPerPixel, "64", "+64" },
            { Id::ShadowsRayTracedSamplesPerPixel, "64", "65" },
            { Id::SkyExposure, "0.5", "0.50" },
            { Id::SkyAutoExposureAdjustmentPeriod, "0.200000003", "0.2" },
            { Id::LightSelectedColor, "0 0.25 0.5", "0  0.25 0.5" },
            { Id::LightSelectedColor, "0 0.25 0.5", " 0 0.25 0.5" },
            { Id::LightSelectedColor, "0 0.25 0.5", "0 0.25 1.1" },
            { Id::LightSelectedColor, "0 0.25 0.5", "0 0.25" },
            { Id::LightSelectedFlashlightEnabled, "off", "maybe" },
            { Id::LightSelectedColor, "0.25 0.5 0.75", "0.25 1.5 -0.5" },
            { Id::LightSelectedColor, "0.25 0.5 0.75", "0.25 inf -0.5" },
            { Id::SkyExposure, "0.5", "nan" },
            { Id::GpuAdapter, "2", "02" },
            { Id::SceneCurrent, "bistro/main.scene.json", "../main.scene.json" },
            { Id::LightSelected, "0:flashlight_1", "00:flashlight_1" },
            { Id::MaterialSelected, "none", "-1" }
        };
        std::string error;
        for (const auto& item : cases)
        {
            const auto& definition = Definition(item.id);
            Require(ValidateSettingsSnapshotCatalogValue(definition, item.valid, error),
                std::string(definition.name) + " rejected canonical value: " + error);
            Require(!ValidateSettingsSnapshotCatalogValue(definition, item.invalid, error),
                std::string(definition.name) + " accepted invalid value " + item.invalid);
        }
        for (const auto& definition : UiSettingsCommandCatalog)
        {
            if (IsSettingsSnapshotValue(definition) && !definition.dynamic &&
                definition.typedDefault.HasValue())
                Require(ValidateSettingsSnapshotCatalogValue(definition,
                    FormatUiSettingsDefaultAnchor(definition), error),
                    std::string(definition.name) + " rejected declared default: " + error);
        }
        const Requests opacity = { { Id::MaterialSelectedOpacity, "2" } };
        Runtime untextured({ { Id::MaterialSelectedOpacity, "1" } });
        auto result = untextured.Apply(opacity);
        Require(result.failureStage == Stage::Preflight && untextured.reads == 0u &&
            untextured.writes == 0u, "untextured opacity must reject before capture");
        Runtime textured({ { Id::MaterialSelectedOpacity, "1" } });
        textured.context = { true, true };
        Require(textured.Apply(opacity).succeeded && textured.values.at(Id::MaterialSelectedOpacity) == "2",
            "textured opacity two must apply");
    }

    void TestMembership()
    {
        DecodedSettings decoded;
        std::vector<Id> expectedIds;
        for (const auto& definition : UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition))
                continue;
            decoded.emplace(definition.name, FormatUiSettingsDefaultAnchor(definition));
            expectedIds.push_back(definition.id);
        }
        Requests transaction;
        std::string error;
        Require(BuildSettingsSnapshotTransaction(decoded, transaction, error) &&
            transaction.size() == expectedIds.size(), "complete canonical membership must build");
        for (std::size_t index = 0u; index < transaction.size(); ++index)
            Require(transaction[index].id == expectedIds[index] &&
                transaction[index].requestedValue == decoded.at(std::string(SettingName(expectedIds[index]))),
                "builder must retain values in canonical catalog order");
        const auto complete = decoded;
        decoded.erase(std::string(SettingName(Id::SkyAmbientFillEnabled)));
        Require(!BuildSettingsSnapshotTransaction(decoded, transaction, error) && transaction.empty() &&
            error.find("missing") != std::string::npos, "missing member must clear the plan");
        for (const char* name : { "unknown.fixture.setting", "ui.settings-collapsed", "reset-settings" })
        {
            decoded = complete;
            decoded.emplace(name, "on");
            Require(!BuildSettingsSnapshotTransaction(decoded, transaction, error) && transaction.empty() &&
                error.find("unknown") != std::string::npos, "unknown or nonpersistent name must reject");
        }
        const std::vector<Requests> invalid = {
            { { Id::Invalid, "on" } },
            { { static_cast<Id>(123u), "on" } },
            { { Id::UiSettingsCollapsed, "on" } },
            { { static_cast<Id>(ActionId::ResetSettings), "on" } },
            { { Id::SkyAmbientFillEnabled, "on" }, { Id::SkyAmbientFillEnabled, "off" } }
        };
        for (const auto& request : invalid)
        {
            Runtime live;
            const auto result = live.Apply(request);
            Require(result.failureStage == Stage::Configuration && live.validations == 0u &&
                live.reads == 0u && live.writes == 0u, "invalid ID membership must reject before access");
        }
    }

    void TestPreflight()
    {
        for (const auto& request : Requests{
            { Id::LightSelectedColor, "0.25 1.5 -0.5" },
            { Id::ShadowsRayTracedSamplesPerPixel, "016" },
            { Id::SkyAmbientFillEnabled, "maybe" },
            { Id::SkyExposure, "0.50" } })
        {
            Runtime live;
            const auto result = live.Apply({ request });
            Require(result.failureStage == Stage::Preflight && live.reads == 0u && live.writes == 0u,
                "invalid value must reject before capture or mutation");
        }
        Runtime late;
        late.rejectValue = Id::NoiseAccumulateSamples;
        const auto result = late.Apply({ { Id::NoisePattern, "spatial-white" },
            { Id::ShadowsRayTracedSamplesPerPixel, "32" }, { Id::NoiseAccumulateSamples, "on" } });
        Require(result.failureStage == Stage::Preflight && late.validations == 3u &&
            late.reads == 0u && late.writes == 0u, "late invalid value must reject the entire payload");

        const Requests adapter = { { Id::SkyAmbientFillEnabled, "on" }, { Id::GpuAdapter, "1" } };
        Runtime live({ { Id::SkyAmbientFillEnabled, "off" }, { Id::GpuAdapter, "0" } });
        const auto mismatch = live.Apply(adapter);
        Require(mismatch.failureStage == Stage::Preflight && live.writes == 0u &&
            mismatch.error.find("-adapter") != std::string::npos,
            "read-only adapter mismatch must precede mutable writes");
        live.values[Id::GpuAdapter] = "1";
        Require(live.Apply(adapter).succeeded && live.writes == 1u,
            "matching adapter precondition must never be written");
        for (unsigned missing = 0u; missing < 4u; ++missing)
        {
            auto access = live.Access();
            if (missing == 0u) access.validateValue = {};
            if (missing == 1u) access.readValue = {};
            if (missing == 2u) access.readRawValue = {};
            if (missing == 3u) access.writeValue = {};
            const auto reads = live.reads;
            Require(Run(adapter, access).failureStage == Stage::Configuration &&
                live.reads == reads && live.writes == 1u, "missing runtime callback must reject immediately");
        }
        Runtime selector({ { Id::SceneCurrent, "san-miguel/main.scene.json" } });
        const auto unresolved = selector.Apply({ { Id::SceneCurrent, "bistro/main.scene.json" } });
        Require(unresolved.failureStage == Stage::Selector && !unresolved.rollbackAttempted &&
            selector.writes == 0u && unresolved.error.find("no selector driver") != std::string::npos,
            "changed selector requires its driver before mutation");
        Runtime capture({ { Id::SkyAmbientFillEnabled, "off" } });
        capture.rejectRead = Id::SkyAmbientFillEnabled;
        Require(capture.Apply({ { Id::SkyAmbientFillEnabled, "on" } }).failureStage == Stage::Capture &&
            capture.writes == 0u, "capture failure must precede writes");
    }

    void TestApplyAndAvailability()
    {
        const Values baseline = {
            { Id::LightingSolution, "ray-marching" }, { Id::NoisePattern, "spatiotemporal-blue" },
            { Id::ShadowsRayTracedSamplesPerPixel, "8" }, { Id::SkyAmbientFillEnabled, "on" },
            { Id::NoiseAccumulateSamples, "off" }, { Id::SceneCurrent, "bistro/main.scene.json" }
        };
        const Values desired = {
            { Id::LightingSolution, "path-tracing" }, { Id::NoisePattern, "spatial-white" },
            { Id::ShadowsRayTracedSamplesPerPixel, "16" }, { Id::SkyAmbientFillEnabled, "on" },
            { Id::NoiseAccumulateSamples, "on" }, { Id::SceneCurrent, "bistro/main.scene.json" }
        };
        Runtime live(baseline);
        const auto applied = live.Apply(Request(desired));
        Require(applied.succeeded && !applied.rollbackAttempted && live.values == desired,
            "mixed renderer settings must apply and verify");
        const auto writes = live.writes;
        const auto repeated = live.Apply(Request(desired));
        Require(repeated.succeeded && repeated.changedValueCount == 0u && live.writes == writes,
            "identical snapshot must issue no setters");
        Require(live.Apply(Request(baseline)).succeeded && live.values == baseline,
            "renderer settings must round-trip exactly");

        const Values available = { { Id::LightSelected, "0:flashlight_1" },
            { Id::LightSelectedFlashlightEnabled, "on" }, { Id::LightSelectedFlashlightBrightness, "400" } };
        Values initial = available;
        initial[Id::LightSelectedFlashlightEnabled] = "off";
        initial[Id::LightSelectedFlashlightBrightness] = "100";
        Runtime dependent(initial);
        Require(dependent.Apply(Request(available)).changedValueCount == 2u && dependent.values == available,
            "available dependent Boolean and numeric values must use ordinary setters");
        Require(dependent.Apply(Request(available)).succeeded && dependent.writes == 2u,
            "available dependent values must be idempotent");
        Values unavailable = available;
        unavailable[Id::LightSelectedFlashlightEnabled] = "<unavailable>";
        unavailable[Id::LightSelectedFlashlightBrightness] = "<unavailable>";
        Runtime absent(unavailable);
        Require(absent.Apply(Request(unavailable)).succeeded && absent.writes == 0u,
            "matching unavailable values must be no-ops");
        for (bool requestedUnavailable : { false, true })
        {
            Runtime mismatch(requestedUnavailable ? available : unavailable);
            const auto result = mismatch.Apply(Request(requestedUnavailable ? unavailable : available));
            Require(result.failureStage == Stage::Preflight && mismatch.writes == 0u &&
                result.error.find("availability") != std::string::npos,
                "either direction of unavailable mismatch must reject before writes");
        }
        Runtime ordinary({ { Id::SkyAmbientFillEnabled, "off" } });
        Require(ordinary.Apply({ { Id::SkyAmbientFillEnabled, "<unavailable>" } }).failureStage == Stage::Preflight &&
            ordinary.writes == 0u, "unavailable sentinel must agree with actual storage");
    }

    void TestFailures()
    {
        const Values baseline = { { Id::NoisePattern, "spatiotemporal-blue" },
            { Id::ShadowsRayTracedSamplesPerPixel, "8" }, { Id::NoiseAccumulateSamples, "off" } };
        const Requests desired = { { Id::NoisePattern, "spatial-white" },
            { Id::ShadowsRayTracedSamplesPerPixel, "32" }, { Id::NoiseAccumulateSamples, "on" } };
        for (const auto fault : { Fault::BeforeWrite, Fault::AfterWrite, Fault::IgnoreWrite, Fault::Rollback })
        {
            Runtime live(baseline);
            live.fault = fault;
            const auto result = live.Apply(desired);
            Require(!result.succeeded && result.rollbackAttempted && live.faultIssued,
                "setter and readback failures must begin rollback");
            Require(result.failureStage == (fault == Fault::IgnoreWrite ? Stage::Readback : Stage::Apply),
                "rollback must preserve the original failure stage");
            if (fault == Fault::Rollback)
                Require(!result.rollbackSucceeded && result.error.find("rollback failed") != std::string::npos,
                    "failed rollback must remain distinguishable from the original failure");
            else
                Require(result.rollbackSucceeded && live.values == baseline,
                    "failed, partially written, or ignored setters must restore the exact baseline");
        }
    }
}

int main()
{
    TestCanonicalValues();
    TestMembership();
    TestPreflight();
    TestApplyAndAvailability();
    TestFailures();
    std::cout << "UVSR settings snapshot value validation passed\n";
}
