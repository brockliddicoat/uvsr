#include "retained_runtime_diagnostic.h"
#include "retained_runtime_json.h"
#include "renderer_runtime_capture.h"
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

    void AssignText(SettingsSnapshotText& output, std::string_view text)
    {
        SettingsSnapshotError error;
        Require(output.Assign(text, error), error.MessageView());
    }

    template<class T> void Append(RetainedRuntimeList<T>& output, T value)
    {
        SettingsSnapshotError error;
        Require(output.Append(std::move(value), error), error.MessageView());
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

        static RetainedRuntimeCases SingleCase(RetainedRuntimeCase runtimeCase)
        {
            RetainedRuntimeCases cases;
            Append(cases, std::move(runtimeCase));
            return cases;
        }

        explicit Driver(RetainedRuntimeCase runtimeCase = {})
            : state(SingleCase(std::move(runtimeCase)), {})
        {
            telemetry.sceneLoaded = true;
            telemetry.pathHistoryGeneration = 1u;
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
            {
                const auto message = EncodeRetainedRuntimeMessageJson("test", result.failure);
                std::cerr << "expected " << int(kind) << ", got " << int(result.kind)
                    << ": " << message.Data() << '\n';
            }
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
        for (const auto build : {BuildIdentityJson, BuildSettingsContractJson})
        {
            json::FailAllocationAfter(0);
            const auto failed = build();
            json::ClearAllocationFailure();
            Require(!failed.IsValid() && failed.Size() == 0 && failed.Data()[0] == '\0' &&
                failed.Failure().code == json::ErrorCode::OutOfMemory,
                "failed diagnostic allocation must publish no text");
            Require(build().IsValid(), "diagnostic output must recover after allocation failure");
        }
        for (const auto arguments : {identityArguments, settingsArguments})
        {
            json::FailAllocationAfter(0);
            const auto failed = TryRunEngineDiagnosticCommand(2, arguments);
            json::ClearAllocationFailure();
            Require(failed.handled && failed.exitCode == 1,
                "failed diagnostic output must stop before renderer startup");
        }
        const auto identityCommand = TryRunEngineDiagnosticCommand(2, identityArguments);
        const auto settingsCommand = TryRunEngineDiagnosticCommand(2, settingsArguments);
        Require(identityCommand.handled && identityCommand.exitCode == 0 &&
            settingsCommand.handled && settingsCommand.exitCode == 0 &&
            !TryRunEngineDiagnosticCommand(3, ordinaryArguments).handled && !TryRunEngineDiagnosticCommand(1, ordinaryArguments).handled,
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
        const auto entries = Member(schema, "entries");
        const auto valueCount = std::count_if(UiSettingsCommandCatalog.begin(), UiSettingsCommandCatalog.end(),
            [](const auto& definition) { return definition.kind != UiSettingsCommandKind::Action; });
        Require(entries.Type() == json::Kind::Array && entries.Count() == size_t(valueCount),
            "settings JSON did not emit every value exactly once");
        std::string previousName;
        bool hasSessionOnly = false;
        for (auto entry = entries.First(); entry.IsValid(); entry = entry.Next())
        {
            RequireExactObject(entry, {"name", "kind", "persistence", "snapshotMember", "defaultValue", "domain"}, "settings entry");
            const auto& name = String(Member(entry, "name"), "name");
            const auto definition = std::find_if(UiSettingsCommandCatalog.begin(), UiSettingsCommandCatalog.end(),
                [&](const auto& candidate) { return candidate.name == name; });
            Require(name > previousName && definition != UiSettingsCommandCatalog.end() &&
                definition->kind != UiSettingsCommandKind::Action, "settings JSON was unsorted, duplicated or included an action");
            Require(Boolean(Member(entry, "snapshotMember"), "membership") == IsSettingsSnapshotValue(*definition) &&
                String(Member(entry, "defaultValue"), "default") == FormatUiSettingsDefault(*definition).View() &&
                String(Member(entry, "domain"), "domain") == FormatUiSettingsDomain(*definition).View() &&
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
        Require(Integer(Member(ParseJson(EncodeRetainedRuntimeStartJson({}, 30)), "schema"),
                "runtime schema") == 4, "runtime record schema must match retained signals");
        RetainedRuntimeCases cases;
        SettingsSnapshotError buildError;
        Require(BuildRetainedRuntimeCases(
            "bistro_interior_retextured.scene.json", "san_miguel_retextured.scene.json", cases, buildError),
            buildError.MessageView());
        Require(cases.Count() == 34, "retained runtime case count: " + std::to_string(cases.Count()));
        std::set<std::string_view> names, scenes;
        std::set<RetainedRuntimeAction> actions;
        bool hasSnapshot = false, hasPath = false, hasFlashlight = false;
        bool hasSky = false;
        for (const auto& c : cases)
        {
            Require(!c.name.View().empty() && names.insert(c.name.View()).second, "unique named runtime cases");
            scenes.insert(c.expectedSceneToken.View());
            actions.insert(c.action);
            hasSnapshot |= c.snapshotRoundTrip;
            hasPath |= c.expectedPathHistoryCount > 0;
            hasFlashlight |= c.expectFlashlightLightingSubmitted;
            hasSky |= c.expectSkyVisibility;
            std::set<SettingId> assigned;
            for (const auto& setting : c.settings)
            {
                const auto* definition = FindSettingsCommandDefinition(setting.id);
                SettingsSnapshotError error;
                Require(definition && assigned.insert(setting.id).second, "unique canonical setting in runtime case");
                if (!ValidateUiSettingsValue(*definition, setting.value, error))
                    Require(false, std::string(c.name.View()) + ": " + std::string(error.MessageView()));
            }
            if (c.actionSettingId != SettingId::Invalid)
            {
                const auto* definition = FindSettingsCommandDefinition(c.actionSettingId);
                SettingsSnapshotError error;
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

    void CaseAllocationFailure()
    {
        for (bool failLists : {false, true})
        {
            RetainedRuntimeCases cases;
            Append(cases, RetainedRuntimeCase{});
            AssignText(cases[0].name, "preserved output");
            SettingsSnapshotError error;
            Require(cases[0].actionValue.SetSelector("preserved long selector bytes", error),
                "prepare existing output");
            const auto* originalCase = cases.Data();
            const auto* originalText = cases[0].actionValue.Text().data();
            std::size_t failures = 0;
            for (; failures < 1024; ++failures)
            {
                if (failLists) FailRetainedCaseAllocationAfter(failures);
                else FailUiSettingsValueAllocationAfter(failures);
                const bool built = BuildRetainedRuntimeCases(
                    "bistro_interior_retextured.scene.json", "san_miguel_retextured.scene.json", cases, error);
                ClearUiSettingsValueAllocationFailure();
                ClearRetainedCaseAllocationFailure();
                if (built) break;
                Require(error.code == SettingsSnapshotErrorCode::OutOfMemory,
                    "case allocation failure is explicit");
                Require(cases.Count() == 1 && cases.Data() == originalCase &&
                    cases[0].name.View() == "preserved output" &&
                    cases[0].actionValue.Text().data() == originalText &&
                    cases[0].actionValue.Text() == "preserved long selector bytes",
                    "failed case preparation preserves the published cases and their views");
            }
            Require(failures > 1 && failures < 1024 && cases.Count() == 34,
                "every typed case allocation failure is recoverable before complete publication");
        }
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
        RetainedRuntimeCases cases;
        Append(cases, RetainedRuntimeCase{});
        auto& c = cases.Back();
        AssignText(c.name, "scene-round-trip");
        c.exerciseRetainedStateChanges = true;
        AssignText(c.actionBaselineSceneToken, "bistro_interior_retextured");
        AssignText(c.expectedSceneToken, "san_miguel_retextured");
        const std::vector<RetainedRuntimeSemanticCaptureView> captures{
            {c.name.View(), c.actionBaselineSceneToken.View(), baseline},
            {c.name.View(), c.expectedSceneToken.View(), changed}};
        const auto check = [&](const std::vector<RetainedRuntimeSemanticCaptureView>& values)
        {
            return CheckRetainedRuntimeSemanticCaptures(cases.Data(), cases.Count(), values.data(), values.size());
        };
        Require(check(captures).Passed(), "distinct scene captures pass");
        auto invalid = captures;
        invalid.pop_back();
        Require(!check(invalid).Passed(), "missing scene capture fails");
        invalid = captures;
        invalid[1].signature = baseline;
        Require(!check(invalid).Passed(), "identical scene output fails");
        invalid = captures;
        invalid[1].sceneToken = c.actionBaselineSceneToken.View();
        Require(!check(invalid).Passed(), "duplicate scene capture fails");
        invalid = captures;
        invalid[1].caseName = "unexpected";
        Require(!check(invalid).Passed(), "unexpected scene case fails");
        RetainedRuntimeSemanticSummary summary;
        summary.BeginCase();
        summary.Record(c, captures[1].sceneToken, captures[1].signature);
        summary.Record(c, captures[0].sceneToken, captures[0].signature);
        summary.CompleteCase(c);
        Require(summary.Validate(cases.Data(), cases.Count()).Passed(), "reversed distinct scene pair passes");
        invalid = captures;
        invalid[1].signature = baseline;
        const auto semantic = check(invalid);
        Require(semantic.failure == RetainedRuntimeSemanticFailure::NotDistinct && semantic.runtimeCase == &c,
            "semantic failure borrows the named case");
        const auto encoded = EncodeRetainedRuntimeSemanticFailureJson("startup", semantic);
        const auto expected = EncodeRetainedRuntimeFailureJson("startup",
            "runtime scene case 'scene-round-trip' lacked distinct scene output");
        Require(encoded.IsValid() && expected.IsValid() && json::SameText(encoded.View(), expected.View()),
            "semantic failure changed final attribution or message bytes");
        json::FailAllocationAfter(0);
        const auto failed = EncodeRetainedRuntimeSemanticFailureJson("startup", semantic);
        json::ClearAllocationFailure();
        Require(!failed.IsValid() && failed.Size() == 0 && failed.Failure().code == json::ErrorCode::OutOfMemory,
            "semantic failure encoding published partial text after allocation failure");
    }

    void SnapshotAndTiming()
    {
        const auto snapshotCase = [] {
            RetainedRuntimeCase result;
            result.snapshotRoundTrip = true;
            return result;
        };
        Driver d(snapshotCase());
        d.telemetry.settingsSnapshot = "configured";
        d.Expect(Kind::ApplyCase);
        d.Settle(Kind::ResetSettings);
        d.telemetry.settingsSnapshot = "defaults";
        d.Expect(Kind::Wait);
        Require(d.Expect(Kind::RestoreSnapshot).snapshot == "configured", "restore uses the saved snapshot");
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
            Driver failed(snapshotCase());
            failed.telemetry.settingsSnapshot = "configured";
            failed.Expect(Kind::ApplyCase);
            failed.Settle(Kind::ResetSettings);
            if (!unchanged)
                failed.telemetry.settingsSnapshot.reset();
            failed.Expect(Kind::Wait);
            failed.Expect(Kind::FinishFail);
        }
        Driver retained(snapshotCase());
        const std::string original("configured\0snapshot bytes", 25);
        std::string canonical = original;
        retained.telemetry.settingsSnapshot = canonical;
        retained.Expect(Kind::ApplyCase);
        retained.Settle(Kind::ResetSettings);
        canonical.assign(128, 'd');
        retained.telemetry.settingsSnapshot = canonical;
        retained.Expect(Kind::Wait);
        Require(retained.Expect(Kind::RestoreSnapshot).snapshot == original,
            "saved snapshot survives replacement of borrowed canonical text");
        canonical = original;
        retained.telemetry.settingsSnapshot = canonical;
        retained.Settle();
        retained.Capture();
        retained.Expect(Kind::FinishPass);
        Driver slow;
        auto c = snapshotCase();
        c.expectSnapshotResetChange = false;
        Driver defaults(std::move(c));
        defaults.telemetry.settingsSnapshot = "defaults";
        defaults.Expect(Kind::ApplyCase);
        defaults.Settle(Kind::ResetSettings);
        defaults.Expect(Kind::Wait);
        Require(defaults.Expect(Kind::RestoreSnapshot).snapshot == "defaults",
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
            Driver d(std::move(c));
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
        for (bool uppercaseNeedle : {false, true})
        {
            RetainedRuntimeCase runtimeCase;
            AssignText(runtimeCase.expectedSceneToken, {uppercaseNeedle ? "B\0C" : "b\0c", 3});
            Driver counted(std::move(runtimeCase));
            const char scene[]{'A', 'B', '\0', 'C', 'D'};
            counted.telemetry.currentScene = std::string_view(scene, sizeof(scene));
            counted.Expect(Kind::ApplyCase);
            counted.Settle(uppercaseNeedle ? Kind::Wait : Kind::CaptureOutput);
            if (!uppercaseNeedle) counted.Capture();
        }
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
                AssignText(c.actionBaselineSceneToken, "bistro");
                AssignText(c.expectedSceneToken, "san_miguel");
            }
            Driver d(std::move(c));
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
            d.Expect(Kind::Wait);
            d.telemetry.pathHistoryGeneration = 2u;
            d.telemetry.pathHistoryCount = 0;
            d.Expect(Kind::Wait);
            d.telemetry.pathHistoryCount = 3;
            d.Settle();
            d.telemetry.output = Output();
            d.Expect(Kind::FinishFail);
            Require(d.state.PassedCaseCount() == 0, "unchanged output cannot prove a visible action");
        }
    }

    void CaptureResetDoesNotProveAction()
    {
        RetainedRuntimeCase c;
        c.action = RetainedRuntimeAction::NudgeCamera;
        c.expectedPathHistoryCount = 3;
        c.requirePathHistoryRestart = true;
        Driver d(std::move(c));
        d.telemetry.pathHistoryCount = 5;
        d.Expect(Kind::ApplyCase);
        d.Settle();
        d.telemetry.pathHistoryGeneration = 2u;
        d.telemetry.pathHistoryCount = 2;
        d.Capture(Kind::ApplyAction);
        d.telemetry.output.reset();
        d.telemetry.lastAppliedAction = RetainedRuntimeAction::NudgeCamera;
        d.telemetry.pathHistoryCount = 3;
        d.Expect(Kind::Wait);
        d.Expect(Kind::Wait);
        d.telemetry.pathHistoryGeneration = 3u;
        d.telemetry.pathHistoryCount = 0;
        d.Expect(Kind::Wait);
        d.telemetry.pathHistoryCount = 3;
        d.Settle();
        d.Capture();
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
            Driver d(std::move(c));
            d.telemetry.flashlightLightingSubmitted = d.telemetry.flashlightVisibilityDispatched = true;
            d.Expect(Kind::ApplyCase);
            d.Settle();
            d.Capture(Kind::ApplyAction);
            d.telemetry.output.reset();
            d.telemetry.lastAppliedAction = RetainedRuntimeAction::ChangeSetting;
            d.Expect(Kind::Wait);
            d.telemetry.flashlightLightingSubmitted = setting != SettingId::LightSelectedFlashlightEnabled;
            d.telemetry.flashlightVisibilityDispatched = false;
            d.Settle();
            d.telemetry.output = Output();
            ++d.telemetry.output->linearHash;
            d.Expect(Kind::ReportCasePass);
        }
        RetainedRuntimeCase c;
        SettingsSnapshotError error;
        UiSettingsValue pattern, resolution;
        Require(pattern.SetToken("spatiotemporal-blue", error) && resolution.SetToken("512x512", error),
            "prepare noise fixture tokens");
        Append(c.settings, {SettingId::NoisePattern, std::move(pattern)});
        Append(c.settings, {SettingId::NoiseResolution, std::move(resolution)});
        Append(c.settings, {SettingId::NoiseAnimateSamples, UiSettingsValue::Boolean(true)});
        c.expectLightingAccumulation = c.assertLightingAccumulationState = true;
        Driver d(std::move(c));
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
        SettingsSnapshotError error;
        UiSettingsValue pattern;
        Require(pattern.SetToken("spatiotemporal-blue", error), "prepare baseline noise token");
        Append(c.settings, {SettingId::NoisePattern, std::move(pattern)});
        c.action = RetainedRuntimeAction::ChangeSetting;
        c.actionSettingId = SettingId::NoisePattern;
        Require(c.actionValue.SetToken("spatial-blue", error), "prepare changed noise token");
        c.expectedPathHistoryCount = 3;
        c.requirePathHistoryRestart = true;
        Driver d(std::move(c));
        d.telemetry.globalNoisePattern = "spatiotemporal-blue";
        d.telemetry.pathHistoryCount = 5;
        d.Expect(Kind::ApplyCase);
        d.Settle();
        const auto action = d.Capture(Kind::ApplyAction);
        Require(action.runtimeCase && action.runtimeCase->actionValue.Text() == "spatial-blue",
            "action directive borrows the retained case value");
        d.telemetry.output.reset();
        d.telemetry.lastAppliedAction = RetainedRuntimeAction::ChangeSetting;
        d.Expect(Kind::Wait);
        d.telemetry.globalNoisePattern = "spatial-blue";
        d.Expect(Kind::Wait);
        d.telemetry.pathHistoryCount = 1;
        d.Expect(Kind::Wait);
        d.telemetry.pathHistoryCount = 3;
        d.Expect(Kind::Wait);
        d.telemetry.pathHistoryGeneration = 2u;
        d.Settle();
        d.Capture();
    }

    void DeterministicCaptureSequence()
    {
        RuntimeCaptureSequence sequence;
        Require(!sequence.IsReady(), "idle capture is not ready");
        Require(sequence.Arm(RuntimeCapturePathDispatchTarget, false), "arm fixed path dispatch sequence");
        Require(sequence.ObservePath(false, false) && sequence.PathDispatchCount() == 0u,
            "no dispatch does not advance capture");
        for (uint32_t dispatch = 1u; dispatch <= RuntimeCapturePathDispatchTarget; ++dispatch)
        {
            Require(sequence.ObservePath(true, dispatch == 1u), "one reset followed by successful dispatches");
            Require(sequence.PathDispatchCount() == dispatch &&
                sequence.IsReady() == (dispatch == RuntimeCapturePathDispatchTarget),
                "capture exactly the selected dispatch, independently of asynchronous accepted counts");
        }
        Require(!sequence.ObservePath(true, false) && !sequence.IsReady(), "capture cannot advance after readiness");
        Require(sequence.Arm(5u, false) && !sequence.ObservePath(true, false), "first dispatch requires reset");
        Require(sequence.Arm(5u, false) && sequence.ObservePath(true, true) &&
            !sequence.ObservePath(true, true), "second reset fails instead of restarting capture");
        Require(sequence.Arm(5u, false) && !sequence.ObservePath(false, true), "reset without dispatch is invalid");
        Require(sequence.Arm(0u, true) && !sequence.ObserveRaster(false, 0u, false, 0u, false, 0u), "required sky dispatch must occur");
        Require(sequence.Arm(0u, true) && !sequence.ObserveRaster(true, 1u, false, 0u, false, 0u), "sky phase must match the selected phase");
        Require(sequence.Arm(0u, true) && sequence.ObserveRaster(true, 0u, false, 0u, false, 0u) &&
            sequence.IsReady() && sequence.SkySamplePhaseValid(), "capture actual phase-zero sky output");
        Require(sequence.Arm(0u, false) && sequence.ObserveRaster(false, 0u, false, 0u, false, 0u) &&
            !sequence.SkySamplePhaseValid(), "inactive sky does not manufacture a captured phase");
        Require(sequence.Arm(0u, true, true, true) &&
            !sequence.ObserveRaster(true, 0u, true, 1u, true, 0u), "directional phase must match");
        Require(sequence.Arm(0u, true, true, true) &&
            !sequence.ObserveRaster(true, 0u, true, 0u, true, 1u), "flashlight phase must match");
        Require(sequence.Arm(0u, true, true, true) &&
            !sequence.ObserveRaster(true, 0u, false, 0u, true, 0u), "required directional dispatch must occur");
        Require(sequence.Arm(0u, true, true, true) &&
            !sequence.ObserveRaster(true, 0u, true, 0u, false, 0u), "required flashlight dispatch must occur");
        Require(sequence.Arm(0u, false) &&
            !sequence.ObserveRaster(false, 0u, true, 0u, false, 0u), "unexpected producer fails");
        Require(sequence.Arm(0u, true, true, true) &&
            sequence.ObserveRaster(true, 0u, true, 0u, true, 0u) &&
            sequence.RasterProducerMask() == 7u && sequence.SkySamplePhase() == 0u &&
            sequence.DirectionalSamplePhase() == 0u && sequence.FlashlightSamplePhase() == 0u,
            "all actual raster phases belong to the captured frame");
        Require(sequence.Arm(0u, false) &&
            sequence.ObserveRaster(false, 99u, false, 99u, false, 99u) &&
            sequence.RasterProducerMask() == 0u && sequence.SkySamplePhase() == 0u &&
            sequence.DirectionalSamplePhase() == 0u && sequence.FlashlightSamplePhase() == 0u,
            "inactive producers clear prior captured phase evidence");
        Require(!sequence.Arm(5u, false, true), "path rejects directional producer");
        Require(!sequence.Arm(5u, false, false, true), "path rejects flashlight producer");
        Require(!sequence.Arm(5u, true), "path and sky capture producers cannot be combined");
        Require(sequence.Arm(5u, false) && !sequence.ObserveRaster(false, 0u, false, 0u, false, 0u), "producer switch fails");
        Require(sequence.Arm(5u, false) && !sequence.Arm(5u, false), "pending capture cannot be overwritten");
        Require(sequence.Arm(UINT32_MAX, false) && sequence.ObservePath(true, true) &&
            sequence.PathDispatchCount() == 1u, "wide target arithmetic does not narrow or wrap");
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
        WindowsPathTextResult provenancePathError;
        constexpr wchar_t provenancePath[] = L"C:/quoted-\"path/uvsr-engine.exe";
        Require(provenance.executablePath.Assign(provenancePath, std::size(provenancePath) - 1,
            WindowsPathTextForm::Native, WindowsPathTextEncoding::Utf8, provenancePathError), "provenance path fixture");
        AssignText(provenance.executableSha256, std::string(64, 'a'));
        RetainedRuntimeCase c;
        AssignText(c.name, "json-\"case");
        RetainedRuntimeTelemetry telemetry;
        telemetry.output = Output();
        WindowsPathTextResult artifactError;
        constexpr wchar_t artifact[] = L"C:\\quoted-\"capture\n.bmp";
        Require(telemetry.output->artifactPath.Assign(artifact, std::size(artifact) - 1,
                WindowsPathTextForm::Native, WindowsPathTextEncoding::Filesystem, artifactError),
            "prepare owned capture artifact text");
        telemetry.output->deterministicCapture = true;
        telemetry.output->capturedPathDispatchCount = 5u;
        telemetry.output->capturedSkySamplePhaseValid = true;
        telemetry.output->capturedSkySamplePhase = 17u;
        telemetry.output->capturedRasterProducerMask = 7u;
        telemetry.output->capturedDirectionalSamplePhase = 23u;
        telemetry.output->capturedFlashlightSamplePhase = 29u;
        telemetry.pathHistoryCount = 99u;
        for (const auto& encoded : {EncodeRetainedRuntimeCaptureJson(0, c, "camera", telemetry),
                EncodeRetainedRuntimeCaseJson(0, c, telemetry)})
        {
            Require(encoded.IsValid(), "runtime image record encoding succeeds");
            const std::string_view record(encoded.Data(), encoded.Size());
            Require(record.find("\"artifactPath\":\"C:\\\\quoted-\\\"capture\\n.bmp\"") != std::string::npos,
                "capture and case JSON escape the retained artifact text");
            Require(record.find("\"capturedPathDispatchCount\":5") != std::string::npos &&
                record.find("\"capturedSkySamplePhase\":17") != std::string::npos &&
                record.find("\"capturedRasterProducerMask\":7") != std::string::npos &&
                record.find("\"capturedDirectionalSamplePhase\":23") != std::string::npos &&
                record.find("\"capturedFlashlightSamplePhase\":29") != std::string::npos &&
                record.find("\"capturedSkySamplePhaseValid\":true") != std::string::npos,
                "captured state comes from image evidence, not later asynchronous telemetry");
        }
        for (const auto& record : {EncodeRetainedRuntimeStartJson(provenance, 92),
                EncodeRetainedRuntimeFailureJson(c.name.View(), "failure\nmessage"),
                EncodeRetainedRuntimeCaptureJson(0, c, "camera", telemetry),
                EncodeRetainedRuntimeCaseJson(0, c, telemetry),
                EncodeRetainedRuntimeSummaryJson(provenance, true, 92, 92, 1234)})
            Require(contract::ParseJson(record).Root().Type() == json::Kind::Object,
                "diagnostic JSONL records remain strict JSON objects");

        const auto checkAllocation = [](auto encode)
        {
            json::FailAllocationAfter(0);
            const auto failed = encode();
            json::ClearAllocationFailure();
            Require(!failed.IsValid() && failed.Size() == 0 &&
                    failed.Failure().code == json::ErrorCode::OutOfMemory,
                "record allocation failure publishes no text");
            json::FailAllocationAfter(1);
            const auto singleAllocation = encode();
            json::ClearAllocationFailure();
            Require(singleAllocation.IsValid(), "one allocation encodes the entire record");
        };
        checkAllocation([&] { return EncodeRetainedRuntimeStartJson(provenance, 92); });
        checkAllocation([&] { return EncodeRetainedRuntimeFailureJson(c.name.View(), "failure\nmessage"); });
        checkAllocation([&] { return EncodeRetainedRuntimeCaptureJson(0, c, "camera", telemetry); });
        checkAllocation([&] { return EncodeRetainedRuntimeCaseJson(0, c, telemetry); });
        checkAllocation([&] { return EncodeRetainedRuntimeSummaryJson(provenance, true, 92, 92, 1234); });
    }
}

int main()
{
    const std::pair<const char*, void(*)()> tests[] = {
        {"engine JSON", EngineJson}, {"table", CaseTable}, {"case allocation", CaseAllocationFailure},
        {"readback", LinearReadback}, {"semantics", SemanticEvidence},
        {"snapshot and timing", SnapshotAndTiming}, {"dispatch and captures", DispatchAndCaptureFailures},
        {"actions", ActionCausality}, {"flashlight and noise", FlashlightAndNoise},
        {"capture reset and action reset", CaptureResetDoesNotProveAction},
        {"noise action phase", NoiseActionPhase}, {"capture sequence", DeterministicCaptureSequence},
        {"timeouts and JSON", TimeoutsAndJson}
    };
    for (const auto& test : tests)
    {
        currentTest = test.first;
        test.second();
    }
    std::cout << "retained runtime diagnostic contracts passed\n";
}
