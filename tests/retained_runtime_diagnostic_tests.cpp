#include "retained_runtime_diagnostic.h"
#include "engine_diagnostics.h"
#include "build_identity.h"
#include "settings_snapshot.h"
#include "settings_snapshot_schema.h"
#include "../tools/strict_json_contract.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <set>

namespace
{
    using namespace uvsr;
    using Kind = RetainedRuntimeDirectiveKind;
    using Clock = RetainedRuntimeDiagnosticState::Clock;
    const char* currentTest = "";

    void Require(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << currentTest << ": " << message << '\n';
            std::exit(1);
        }
    }

    RuntimeOutputEvidence Output()
    {
        RuntimeOutputEvidence output;
        output.valid = true;
        output.width = output.height = 2;
        output.pixelBytes = 16;
        output.minimumByte = 1;
        output.maximumByte = 2;
        output.linearReadbackValid = true;
        output.linearHash = 100;
        return output;
    }

    struct Driver
    {
        RetainedRuntimeDiagnosticState state;
        RetainedRuntimeTelemetry telemetry;
        int milliseconds = 0;

        explicit Driver(RetainedRuntimeCase runtimeCase = {}) : state({runtimeCase}, {})
        {
            telemetry.sceneLoaded = true;
            telemetry.cpuFrameMilliseconds = 10;
            telemetry.gpuFrameMilliseconds = 8;
            telemetry.gpuFrameTimingAvailable = true;
        }

        RetainedRuntimeDirective Tick()
        {
            return state.Tick(telemetry, Clock::time_point{} + std::chrono::milliseconds(milliseconds++));
        }

        RetainedRuntimeDirective Expect(Kind kind)
        {
            const auto result = Tick();
            if (result.kind != kind)
                std::cerr << "expected " << int(kind) << ", got " << int(result.kind)
                    << ": " << result.payload << '\n';
            Require(result.kind == kind, "runtime directive");
            return result;
        }

        void Settle(Kind next = Kind::CaptureOutput)
        {
            Expect(Kind::Wait);
            Expect(next);
        }

        RetainedRuntimeDirective Capture(Kind next = Kind::ReportCasePass)
        {
            telemetry.output = Output();
            return Expect(next);
        }
    };

    void EngineJson()
    {
        using namespace contract;
        const char* identityArguments[] = {"uvsr-engine.exe", "--identity-json"};
        const char* settingsArguments[] = {"uvsr-engine.exe", "--settings-contract-json"};
        const char* ordinaryArguments[] = {"uvsr-engine.exe", "-width", "1280"};
        Require(TryRunEngineDiagnosticCommand(2, identityArguments) == std::optional<int>{0} &&
            TryRunEngineDiagnosticCommand(2, settingsArguments) == std::optional<int>{0} &&
            !TryRunEngineDiagnosticCommand(3, ordinaryArguments) && !TryRunEngineDiagnosticCommand(1, ordinaryArguments),
            "diagnostic commands failed or consumed ordinary renderer arguments");

        const auto identity = ParseJson(BuildIdentityJson());
        RequireExactObject(identity, {"executable", "source_commit", "source_identity", "source_tree_clean",
            "production", "configuration", "settings_hash", "engine_version", "product_version"}, "identity JSON");
        for (const auto& field : std::initializer_list<std::pair<std::string_view, std::string_view>>{
                {"executable", "uvsr-engine.exe"}, {"source_commit", GetBuiltSourceCommit()},
                {"source_identity", GetBuiltSourceIdentity()}, {"configuration", GetBuiltConfiguration()},
                {"settings_hash", GetBuiltSettingsNumberHash()}, {"engine_version", GetBuiltEngineVersion()},
                {"product_version", GetBuiltEngineProductVersion()}})
            Require(String(Member(identity, field.first), field.first) == field.second, "emitted identity lost its built value");
        Require(Boolean(Member(identity, "source_tree_clean"), "clean") == IsBuiltSourceTreeClean() &&
            Boolean(Member(identity, "production"), "production") == IsBuiltProduction(), "identity lost its typed build mode");

        const auto schema = ParseJson(BuildSettingsContractJson());
        RequireExactObject(schema, {"schemaVersion", "settingsHash", "engineVersion", "serializationPolicy", "entries"}, "settings JSON");
        Require(Integer(Member(schema, "schemaVersion"), "schema") == SettingsSnapshotVersion &&
            String(Member(schema, "settingsHash"), "hash") == GetSettingsNumberHashText() &&
            String(Member(schema, "engineVersion"), "version") == FormatEngineVersion(CurrentEngineVersion) &&
            String(Member(schema, "serializationPolicy"), "policy") == SettingsSnapshotSerializationPolicy,
            "settings JSON lost authoritative schema identity or serialization policy");
        const auto& entries = Member(schema, "entries");
        const auto valueCount = std::count_if(UiSettingsCommandCatalog.begin(), UiSettingsCommandCatalog.end(),
            [](const auto& definition) { return definition.kind != UiSettingsCommandKind::Action; });
        Require(entries.kind == JsonValue::Kind::Array && entries.array.size() == size_t(valueCount),
            "settings JSON did not emit every value exactly once");
        std::string previousName;
        bool hasSessionOnly = false;
        for (const auto& entry : entries.array)
        {
            RequireExactObject(entry, {"name", "kind", "persistence", "snapshotMember", "defaultValue", "domain"}, "settings entry");
            const auto& name = String(Member(entry, "name"), "name");
            const auto definition = std::find_if(UiSettingsCommandCatalog.begin(), UiSettingsCommandCatalog.end(),
                [&](const auto& candidate) { return candidate.name == name; });
            Require(name > previousName && definition != UiSettingsCommandCatalog.end() &&
                definition->kind != UiSettingsCommandKind::Action, "settings JSON was unsorted, duplicated or included an action");
            Require(Boolean(Member(entry, "snapshotMember"), "membership") == IsSettingsSnapshotValue(*definition) &&
                String(Member(entry, "defaultValue"), "default") == FormatUiSettingsDefault(*definition) &&
                String(Member(entry, "domain"), "domain") == FormatUiSettingsDomain(*definition) &&
                !String(Member(entry, "kind"), "kind").empty(), "settings JSON lost typed membership or formatted values");
            const auto& persistence = String(Member(entry, "persistence"), "persistence");
            Require(persistence == (definition->persistence == UiSettingsPersistence::SnapshotCatalog ? "SnapshotCatalog" :
                definition->persistence == UiSettingsPersistence::SessionOnly ? "SessionOnly" : "None"),
                "settings JSON changed its persistence domain");
            hasSessionOnly |= definition->persistence == UiSettingsPersistence::SessionOnly;
            previousName = name;
        }
        Require(hasSessionOnly, "settings JSON omitted session-only controls");
    }

    void CaseTable()
    {
        using namespace uvsr::contract;
        Require(Integer(Member(ParseJson(BuildRetainedRuntimeStartJson({}, 30)), "schema"),
                "runtime schema") == 4, "runtime record schema must match retained signals");
        const auto cases = BuildRetainedRuntimeCases(
            "bistro_interior_retextured.scene.json", "san_miguel_retextured.scene.json");
        Require(cases.size() == 34, "retained runtime case count: " + std::to_string(cases.size()));
        std::set<std::string> names, scenes;
        std::set<RetainedRuntimeAction> actions;
        bool hasSnapshot = false, hasPath = false, hasFlashlight = false;
        bool hasSky = false;
        for (const auto& c : cases)
        {
            Require(!c.name.empty() && names.insert(c.name).second, "unique named runtime cases");
            scenes.insert(c.expectedSceneToken);
            actions.insert(c.action);
            hasSnapshot |= c.snapshotRoundTrip;
            hasPath |= c.expectedPathHistoryCount > 0;
            hasFlashlight |= c.expectFlashlightLightingSubmitted;
            hasSky |= c.expectSkyVisibility;
            std::set<SettingId> assigned;
            for (const auto& setting : c.settings)
            {
                const auto* definition = FindSettingsCommandDefinition(setting.id);
                std::string error;
                Require(definition && assigned.insert(setting.id).second, "unique canonical setting in runtime case");
                if (!ValidateUiSettingsValue(*definition, setting.value, error))
                    Require(false, c.name + ": " + error);
            }
            if (c.actionSettingId != SettingId::Invalid)
            {
                const auto* definition = FindSettingsCommandDefinition(c.actionSettingId);
                std::string error;
                Require(definition && ValidateUiSettingsValue(*definition, c.actionValue, error),
                    "runtime action belongs to its typed domain");
            }
        }
        Require(scenes.count("bistro_interior_retextured") && scenes.count("san_miguel_retextured"),
            "both retained scenes have runtime evidence");
        Require(hasSnapshot && hasPath && hasFlashlight && hasSky,
            "case table retains snapshot, path, flashlight, sky proof");
        for (auto action : {RetainedRuntimeAction::NudgeCamera, RetainedRuntimeAction::ResizeViewport,
                RetainedRuntimeAction::ChangeScene, RetainedRuntimeAction::ChangeSetting,
                RetainedRuntimeAction::ChangeMaterial, RetainedRuntimeAction::ChangeLight})
            Require(actions.count(action) != 0, "runtime mutation coverage");
    }

    void LinearReadback()
    {
        const std::array<std::uint16_t, 16> half = {
            0, 0, 0, 0x3c00, 0x3c00, 0, 0, 0x3c00,
            0, 0x3800, 0, 0x3c00, 0, 0, 0x4000, 0x3c00
        };
        const std::array<float, 16> full = {0,0,0,1, 1,0,0,1, 0,0.5f,0,1, 0,0,2,1};
        for (const auto& output : {AnalyzeRuntimeLinearRgba16(half.data(), 2, 2, 16),
                AnalyzeRuntimeLinearRgba32(full.data(), 2, 2, 32)})
        {
            Require(output.linearReadbackValid && output.finiteComponentCount == 16 &&
                output.varyingPixelCount == 3 && output.edgePixelCount == 2, "finite varying readback");
            Require(output.meanLinearLuminance > 0.178 && output.meanLinearLuminance < 0.179 &&
                output.rmsLinearLuminance > 0.220 && output.rmsLinearLuminance < 0.221 &&
                output.meanLinearHorizontalGradient == 1.75, "linear image statistics");
        }
        auto badHalf = half;
        badHalf[0] = 0x7c00;
        auto badFull = full;
        badFull[0] = std::numeric_limits<float>::infinity();
        Require(!AnalyzeRuntimeLinearRgba16(badHalf.data(), 2, 2, 16).linearReadbackValid &&
            !AnalyzeRuntimeLinearRgba32(badFull.data(), 2, 2, 32).linearReadbackValid,
            "nonfinite readback fails");
        Require(!AnalyzeRuntimeLinearRgba16(half.data(), 2, 2, 8).linearReadbackValid &&
            !AnalyzeRuntimeLinearRgba32(full.data(), 2, 2, 16).linearReadbackValid,
            "short row pitch fails");
        const RuntimeLinearReadbackLayout layout{640, 360, 38, 1};
        Require(RuntimeLinearReadbackLayoutsMatch(layout, layout), "identical readback layout can be reused");
        for (const auto& other : {RuntimeLinearReadbackLayout{641,360,38,1}, {640,361,38,1},
                {640,360,49,1}, {640,360,38,2}})
            Require(!RuntimeLinearReadbackLayoutsMatch(layout, other), "readback reuse binds extent, format and samples");
    }

    void SemanticEvidence()
    {
        auto output = Output();
        output.meanLinearLuminance = 0.25;
        output.rmsLinearLuminance = 0.5;
        output.meanLinearHorizontalGradient = 0.125;
        const auto baseline = BuildRuntimeSemanticSignature(output);
        auto changed = baseline;
        changed.meanLinearLuminance *= 1.0005;
        Require(!RuntimeSemanticSignaturesAreDistinct(baseline, changed), "image comparisons tolerate small jitter");
        changed.meanLinearLuminance *= 1.02;
        Require(RuntimeSemanticSignaturesAreDistinct(baseline, changed), "visible changes are distinct");
        RetainedRuntimeCase c;
        c.name = "scene-round-trip";
        c.exerciseRetainedStateChanges = true;
        c.actionBaselineSceneToken = "bistro_interior_retextured";
        c.expectedSceneToken = "san_miguel_retextured";
        const std::vector<RetainedRuntimeCase> cases{c};
        const std::vector<RetainedRuntimeSemanticCapture> captures{
            {c.name, c.actionBaselineSceneToken, baseline},
            {c.name, c.expectedSceneToken, changed}};
        std::string reason;
        Require(ValidateRetainedRuntimeSemanticCaptures(cases, captures, reason), reason);
        auto invalid = captures;
        invalid.pop_back();
        Require(!ValidateRetainedRuntimeSemanticCaptures(cases, invalid, reason), "missing scene capture fails");
        invalid = captures;
        invalid[1].signature = baseline;
        Require(!ValidateRetainedRuntimeSemanticCaptures(cases, invalid, reason), "identical scene output fails");
        invalid = captures;
        invalid[1].sceneToken = c.actionBaselineSceneToken;
        Require(!ValidateRetainedRuntimeSemanticCaptures(cases, invalid, reason), "duplicate scene capture fails");
        invalid = captures;
        invalid[1].caseName = "unexpected";
        Require(!ValidateRetainedRuntimeSemanticCaptures(cases, invalid, reason), "unexpected scene case fails");
    }

    void SnapshotAndTiming()
    {
        RetainedRuntimeCase c;
        c.snapshotRoundTrip = true;
        Driver d(c);
        d.telemetry.settingsSnapshot = "configured";
        d.Expect(Kind::ApplyCase);
        d.Settle(Kind::ResetSettings);
        d.telemetry.settingsSnapshot = "defaults";
        d.Expect(Kind::Wait);
        Require(d.Expect(Kind::RestoreSnapshot).payload == "configured", "restore uses the saved snapshot");
        d.telemetry.settingsSnapshot = "configured";
        d.telemetry.gpuFrameTimingAvailable = false;
        d.Expect(Kind::Wait);
        d.Expect(Kind::Wait);
        Require(d.state.RequiresSettingsSnapshot(), "restore remains observable while timing is unavailable");
        d.telemetry.gpuFrameTimingAvailable = true;
        d.Settle();
        d.telemetry.gpuFrameTimingAvailable = false;
        d.telemetry.cpuFrameMilliseconds = d.telemetry.gpuFrameMilliseconds = 5000;
        const auto result = d.Capture();
        Require(result.hasStableFrameTiming && result.stableCpuFrameMilliseconds == 10 &&
            result.stableGpuFrameMilliseconds == 8, "capture overhead does not replace settled timing");
        d.Expect(Kind::FinishPass);
        Require(d.state.PassedCaseCount() == 1, "only proven cases pass");
        for (bool unchanged : {false, true})
        {
            Driver failed(c);
            failed.telemetry.settingsSnapshot = "configured";
            failed.Expect(Kind::ApplyCase);
            failed.Settle(Kind::ResetSettings);
            if (!unchanged)
                failed.telemetry.settingsSnapshot.reset();
            failed.Expect(Kind::Wait);
            failed.Expect(Kind::FinishFail);
        }
        Driver slow;
        c.expectSnapshotResetChange = false;
        Driver defaults(c);
        defaults.telemetry.settingsSnapshot = "defaults";
        defaults.Expect(Kind::ApplyCase);
        defaults.Settle(Kind::ResetSettings);
        defaults.Expect(Kind::Wait);
        Require(defaults.Expect(Kind::RestoreSnapshot).payload == "defaults",
            "factory-default reset remains unchanged and still restores its snapshot");
        slow.telemetry.cpuFrameMilliseconds = slow.telemetry.gpuFrameMilliseconds = 1200;
        slow.Expect(Kind::ApplyCase);
        slow.Settle(Kind::Wait);
        slow.Expect(Kind::FinishFail);
    }

    void DispatchAndCaptureFailures()
    {
        struct Dispatch
        {
            bool RetainedRuntimeCase::* required;
            bool RetainedRuntimeTelemetry::* observed;
        };
        const Dispatch dispatches[] = {
            {&RetainedRuntimeCase::expectDirectionalVisibility, &RetainedRuntimeTelemetry::directionalVisibilityDispatched},
            {&RetainedRuntimeCase::expectSkyVisibility, &RetainedRuntimeTelemetry::skyVisibilityDispatched},
            {&RetainedRuntimeCase::expectFlashlightVisibility, &RetainedRuntimeTelemetry::flashlightVisibilityDispatched},
            {&RetainedRuntimeCase::expectLightingAccumulation, &RetainedRuntimeTelemetry::lightingAccumulationCommitted}
        };
        for (const auto& dispatch : dispatches)
        {
            RetainedRuntimeCase c;
            c.*dispatch.required = true;
            Driver d(c);
            d.Expect(Kind::ApplyCase);
            d.Expect(Kind::Wait);
            d.Expect(Kind::Wait);
            d.telemetry.*dispatch.observed = true;
            d.Settle();
            d.Capture();
        }
        const std::function<void(RuntimeOutputEvidence&)> corruptions[] = {
            [](auto& o) {o.valid = false;}, [](auto& o) {o.pixelBytes = 0;},
            [](auto& o) {o.maximumByte = o.minimumByte;},
            [](auto& o) {o.linearReadbackValid = false;}, [](auto& o) {o.nonFiniteComponentCount = 1;}
        };
        for (const auto& corrupt : corruptions)
        {
            Driver d;
            d.Expect(Kind::ApplyCase);
            d.Settle();
            d.telemetry.output = Output();
            corrupt(*d.telemetry.output);
            d.Expect(Kind::FinishFail);
            Require(d.state.PassedCaseCount() == 0, "invalid output never counts as proof");
        }
    }

    void ActionCausality()
    {
        for (auto action : {RetainedRuntimeAction::NudgeCamera, RetainedRuntimeAction::ChangeScene,
                RetainedRuntimeAction::ResizeViewport})
        {
            RetainedRuntimeCase c;
            c.action = action;
            c.expectedPathHistoryCount = 3;
            c.requirePathHistoryRestart = true;
            c.requireActionOutputDifference = true;
            c.resizeWidth = c.resizeHeight = 2;
            if (action == RetainedRuntimeAction::ChangeScene)
            {
                c.actionBaselineSceneToken = "bistro";
                c.expectedSceneToken = "san_miguel";
            }
            Driver d(c);
            d.telemetry.currentScene = "bistro.scene.json";
            d.telemetry.pathHistoryCount = 5;
            d.Expect(Kind::ApplyCase);
            d.Settle();
            Require(d.Capture(Kind::ApplyAction).action == action, "baseline capture precedes the named action");
            d.telemetry.output.reset();
            d.telemetry.currentScene = "san_miguel.scene.json";
            d.telemetry.lastAppliedAction = RetainedRuntimeAction::ChangeSetting;
            d.telemetry.pathHistoryCount = 1;
            d.Expect(Kind::Wait);
            d.telemetry.lastAppliedAction = action;
            d.telemetry.pathHistoryCount = 5;
            d.Expect(Kind::Wait);
            d.telemetry.pathHistoryCount = 1;
            d.Expect(Kind::Wait);
            d.telemetry.pathHistoryCount = 3;
            d.Settle();
            d.telemetry.output = Output();
            d.Expect(Kind::FinishFail);
            Require(d.state.PassedCaseCount() == 0, "unchanged output cannot prove a visible action");
        }
    }

    void FlashlightAndNoise()
    {
        for (auto setting : {SettingId::LightSelectedFlashlightEnabled, SettingId::LightSelectedFlashlightCastShadows})
        {
            RetainedRuntimeCase c;
            c.action = RetainedRuntimeAction::ChangeSetting;
            c.actionSettingId = setting;
            c.actionValue = UiSettingsValue::Boolean(false);
            c.expectFlashlightLightingSubmitted = c.assertFlashlightLightingState = true;
            c.expectFlashlightVisibility = c.assertFlashlightVisibilityState = true;
            c.requireActionOutputDifference = true;
            Driver d(c);
            d.telemetry.flashlightLightingSubmitted = d.telemetry.flashlightVisibilityDispatched = true;
            d.Expect(Kind::ApplyCase);
            d.Settle();
            d.Capture(Kind::ApplyAction);
            d.telemetry.output.reset();
            d.telemetry.lastAppliedAction = c.action;
            d.Expect(Kind::Wait);
            d.telemetry.flashlightLightingSubmitted = setting != SettingId::LightSelectedFlashlightEnabled;
            d.telemetry.flashlightVisibilityDispatched = false;
            d.Settle();
            d.telemetry.output = Output();
            ++d.telemetry.output->linearHash;
            d.Expect(Kind::ReportCasePass);
        }
        RetainedRuntimeCase c;
        c.settings = {{SettingId::NoisePattern, UiSettingsValue::Token("spatiotemporal-blue")},
            {SettingId::NoiseResolution, UiSettingsValue::Token("512x512")},
            {SettingId::NoiseAnimateSamples, UiSettingsValue::Boolean(true)}};
        c.expectLightingAccumulation = c.assertLightingAccumulationState = true;
        Driver d(c);
        d.Expect(Kind::ApplyCase);
        d.Expect(Kind::Wait);
        d.telemetry.globalNoisePattern = "spatiotemporal-blue";
        d.telemetry.globalNoiseResolution = "512x512";
        d.telemetry.globalNoiseAnimateSamples = true;
        d.telemetry.lightingAccumulationCommitted = true;
        d.Settle();
        d.Capture();
    }

    void NoiseActionPhase()
    {
        RetainedRuntimeCase c;
        c.settings = {{SettingId::NoisePattern, UiSettingsValue::Token("spatiotemporal-blue")}};
        c.action = RetainedRuntimeAction::ChangeSetting;
        c.actionSettingId = SettingId::NoisePattern;
        c.actionValue = UiSettingsValue::Token("spatial-blue");
        c.expectedPathHistoryCount = 3;
        c.requirePathHistoryRestart = true;
        Driver d(c);
        d.telemetry.globalNoisePattern = "spatiotemporal-blue";
        d.telemetry.pathHistoryCount = 5;
        d.Expect(Kind::ApplyCase);
        d.Settle();
        d.Capture(Kind::ApplyAction);
        d.telemetry.output.reset();
        d.telemetry.lastAppliedAction = c.action;
        d.Expect(Kind::Wait);
        d.telemetry.globalNoisePattern = "spatial-blue";
        d.Expect(Kind::Wait);
        d.telemetry.pathHistoryCount = 1;
        d.Expect(Kind::Wait);
        d.telemetry.pathHistoryCount = 3;
        d.Settle();
        d.Capture();
    }

    void TimeoutsAndJson()
    {
        Driver perCase;
        perCase.Expect(Kind::ApplyCase);
        Require(perCase.state.Tick({}, Clock::time_point{} + std::chrono::minutes(6)).kind == Kind::FinishFail,
            "per-case timeout is bounded");
        for (int over : {0, 1})
        {
            Driver global;
            Require(global.state.Tick({}, Clock::time_point{} + std::chrono::hours(6) +
                std::chrono::milliseconds(over)).kind == (over ? Kind::FinishFail : Kind::ApplyCase),
                "global timeout boundary");
        }
        RetainedRuntimeProvenance provenance;
        provenance.executablePath = "C:/quoted-\"path/uvsr-engine.exe";
        provenance.executableSha256 = std::string(64, 'a');
        RetainedRuntimeCase c;
        c.name = "json-\"case";
        RetainedRuntimeTelemetry telemetry;
        telemetry.output = Output();
        for (const auto& record : {BuildRetainedRuntimeStartJson(provenance, 92),
                BuildRetainedRuntimeFailureJson(c.name, "failure\nmessage"),
                BuildRetainedRuntimeCaptureJson(0, c, "camera", telemetry),
                BuildRetainedRuntimeCaseJson(0, c, telemetry),
                BuildRetainedRuntimeSummaryJson(provenance, true, 92, 92, 1234)})
            Require(contract::ParseJson(record).kind == contract::JsonValue::Kind::Object,
                "diagnostic JSONL records remain strict JSON objects");
    }
}

int main()
{
    const std::pair<const char*, void(*)()> tests[] = {
        {"engine JSON", EngineJson}, {"table", CaseTable}, {"readback", LinearReadback}, {"semantics", SemanticEvidence},
        {"snapshot and timing", SnapshotAndTiming}, {"dispatch and captures", DispatchAndCaptureFailures},
        {"actions", ActionCausality}, {"flashlight and noise", FlashlightAndNoise},
        {"noise action phase", NoiseActionPhase}, {"timeouts and JSON", TimeoutsAndJson}
    };
    for (const auto& test : tests)
    {
        currentTest = test.first;
        test.second();
    }
    std::cout << "retained runtime diagnostic contracts passed\n";
}
