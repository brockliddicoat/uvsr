#include "settings_snapshot_transaction.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Settings snapshot transaction validation failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    struct FakeSettings
    {
        std::map<std::string, std::string, std::less<>> values;
        std::string failOnceOnWrite;
        bool mutateBeforeFailure = false;
        std::size_t readCount = 0u;
        std::size_t writeCount = 0u;

        bool Read(
            std::string_view name,
            std::string& value,
            std::string& error)
        {
            ++readCount;
            const auto found = values.find(name);
            if (found == values.end())
            {
                error = "missing fake value";
                return false;
            }
            value = found->second;
            return true;
        }

        bool Write(
            std::string_view name,
            std::string_view value,
            std::string& error)
        {
            ++writeCount;
            const auto found = values.find(name);
            if (found == values.end())
            {
                error = "missing fake value";
                return false;
            }
            if (name == failOnceOnWrite)
            {
                failOnceOnWrite.clear();
                if (mutateBeforeFailure)
                    found->second.assign(value.data(), value.size());
                error = "injected SET failure";
                return false;
            }
            found->second.assign(value.data(), value.size());
            return true;
        }
    };

    uvsr::SettingsSnapshotTransactionResult Apply(
        const std::vector<uvsr::SettingsSnapshotTransactionEntry>& transaction,
        FakeSettings& settings,
        uvsr::SettingsSnapshotValueValidator validator = {})
    {
        if (!validator)
        {
            validator = [](
                std::string_view,
                std::string_view,
                std::string&)
            {
                return true;
            };
        }
        return uvsr::ApplySettingsSnapshotTransaction(
            transaction,
            validator,
            [&settings](
                std::string_view name,
                std::string& value,
                std::string& error)
            {
                return settings.Read(name, value, error);
            },
            [&settings](
                std::string_view name,
                std::string_view value,
                std::string& error)
            {
                return settings.Write(name, value, error);
            });
    }
}

int main()
{
    using namespace uvsr;
    using Mode = SettingsSnapshotApplicationMode;

    const std::vector<SettingsSnapshotCatalogEntry> smallCatalog = {
        { "lighting.solution", Mode::Mutable },
        { "scene.current", Mode::Selector },
        { "visibility.ao.enabled", Mode::Mutable }
    };
    const DecodedSettings complete = {
        { "lighting.solution", "path-tracing" },
        { "scene.current", "bistro/main.scene.json" },
        { "visibility.ao.enabled", "on" }
    };
    std::vector<SettingsSnapshotTransactionEntry> transaction;
    std::string error;

    const auto findDefinition = [](std::string_view name)
        -> const UiSettingsCommandDefinition&
    {
        for (const UiSettingsCommandDefinition& definition :
            UiSettingsCommandCatalog)
        {
            if (definition.name == name)
                return definition;
        }
        Fail("catalog definition fixture was not found");
    };
    const auto validateCatalogValue = [&findDefinition](
        std::string_view name,
        std::string_view value,
        std::string& validationError)
    {
        return ValidateSettingsSnapshotCatalogValue(
            findDefinition(name), value, validationError);
    };
    Require(
        ResolveSettingsSnapshotApplicationMode(
            findDefinition("scene.current")) ==
                Mode::Selector &&
        ResolveSettingsSnapshotApplicationMode(
            findDefinition("light.selected.flashlight.enabled")) ==
                Mode::LiveDependent &&
        ResolveSettingsSnapshotApplicationMode(
            findDefinition("visibility.ao.enabled")) == Mode::Mutable,
        "catalog kind and dependency metadata must select an explicit "
        "application mode");
    Require(
        ValidateSettingsSnapshotCatalogValue(
            findDefinition("lighting.solution"),
            "path-tracing",
            error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("lighting.solution"),
            "Path Tracing",
            error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("lighting.solution"),
            "unknown-solution",
            error),
        "enum preflight must require the exact authoritative catalog token");
    Require(
        ValidateSettingsSnapshotCatalogValue(
            findDefinition("ui.skin"), "ogg", error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("ui.skin"), "og", error),
        "enum preflight must accept only the catalog's canonical Ogg token");
    Require(
        ValidateSettingsSnapshotCatalogValue(
            findDefinition("visibility.samples"), "64", error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("visibility.samples"), "064", error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("visibility.samples"), "+64", error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("visibility.samples"), "65", error) &&
        ValidateSettingsSnapshotCatalogValue(
            findDefinition("anti-aliasing.taa.history.frames"),
            "-1",
            error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("anti-aliasing.taa.history.frames"),
            "0",
            error),
        "integer preflight must enforce canonical decimal syntax, ranges, "
        "and explicit alternatives");
    Require(
        ValidateSettingsSnapshotCatalogValue(
            findDefinition("visibility.thickness"), "0.5", error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("visibility.thickness"), "0.50", error) &&
        ValidateSettingsSnapshotCatalogValue(
            findDefinition("sky.auto-exposure.adjustment-period"),
            "0.200000003",
            error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("sky.auto-exposure.adjustment-period"),
            "0.2",
            error),
        "float preflight must require the max_digits10 GET representation");
    Require(
        ValidateSettingsSnapshotCatalogValue(
            findDefinition("ui.accent.primary"),
            "0 0.25 0.5 1",
            error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("ui.accent.primary"),
            "0  0.25 0.5 1",
            error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("ui.accent.primary"),
            " 0 0.25 0.5 1",
            error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("ui.accent.primary"),
            "0 0.25 0.5 1.1",
            error),
        "vector preflight must require single-space canonical components, "
        "count, and range");
    Require(
        ValidateSettingsSnapshotCatalogValue(
            findDefinition("light.selected.flashlight.enabled"),
            "on",
            error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("light.selected.flashlight.enabled"),
            "maybe",
            error) &&
        ValidateSettingsSnapshotCatalogValue(
            findDefinition("light.selected.color"),
            "0.25 1.5 -0.5",
            error) &&
        !ValidateSettingsSnapshotCatalogValue(
            findDefinition("light.selected.color"),
            "0.25 inf -0.5",
            error),
        "live-dependent values must use their ordinary boolean/numeric "
        "domains rather than bypass preflight");
    for (const UiSettingsCommandDefinition& definition :
        UiSettingsCommandCatalog)
    {
        if (definition.persistence !=
                UiSettingsPersistence::SnapshotCatalog ||
            definition.dynamic ||
            definition.defaultValue.rfind("automatic-", 0u) == 0u)
            continue;
        Require(
            ValidateSettingsSnapshotCatalogValue(
                definition,
                definition.defaultValue,
                error),
            "every mutable authoritative default must pass the same "
            "mutation-free snapshot preflight: " +
                std::string(definition.name) + " / " + error);
    }
    Require(
        BuildSettingsSnapshotTransaction(
            complete, smallCatalog, transaction, error) &&
            transaction.size() == smallCatalog.size() &&
            transaction[1].mode == Mode::Selector,
        "complete decoded membership must produce a catalog-ordered plan");

    DecodedSettings missing = complete;
    missing.erase("visibility.ao.enabled");
    Require(
        !BuildSettingsSnapshotTransaction(
            missing, smallCatalog, transaction, error) &&
            error.find("missing") != std::string::npos,
        "missing catalog membership must fail before mutation");

    DecodedSettings unknown = complete;
    unknown.emplace("unknown.fixture.setting", "invalid-fixture-value");
    Require(
        !BuildSettingsSnapshotTransaction(
            unknown, smallCatalog, transaction, error) &&
            error.find("unknown") != std::string::npos,
        "unknown catalog membership must fail before mutation");

    Require(
        !BuildSettingsSnapshotTransaction(
            complete,
            {
                { "lighting.solution", Mode::Mutable },
                { "lighting.solution", Mode::Mutable },
                { "scene.current", Mode::Selector }
            },
            transaction,
            error) &&
            error.find("duplicate") != std::string::npos,
        "an invalid authoritative catalog must fail closed");

    const std::vector<SettingsSnapshotCatalogEntry> retainedCatalog = {
        { "lighting.solution", Mode::Mutable },
        { "visibility.quality", Mode::Mutable },
        { "visibility.samples", Mode::Mutable },
        { "visibility.ao.enabled", Mode::Mutable },
        { "visibility.gi.enabled", Mode::Mutable },
        { "denoising.ao.method", Mode::Mutable },
        { "denoising.gi.method", Mode::Mutable },
        { "scene.current", Mode::Selector }
    };
    const DecodedSettings defaults = {
        { "lighting.solution", "ray-marching" },
        { "visibility.quality", "high" },
        { "visibility.samples", "8" },
        { "visibility.ao.enabled", "on" },
        { "visibility.gi.enabled", "off" },
        { "denoising.ao.method", "joint-bilateral" },
        { "denoising.gi.method", "raw" },
        { "scene.current", "bistro/main.scene.json" }
    };
    const DecodedSettings nondefault = {
        { "lighting.solution", "path-tracing" },
        { "visibility.quality", "custom" },
        { "visibility.samples", "16" },
        { "visibility.ao.enabled", "on" },
        { "visibility.gi.enabled", "on" },
        { "denoising.ao.method", "reblur" },
        { "denoising.gi.method", "relax" },
        { "scene.current", "bistro/main.scene.json" }
    };
    FakeSettings live;
    live.values = defaults;
    DecodedSettings noncanonical = defaults;
    noncanonical["visibility.samples"] = "016";
    Require(
        BuildSettingsSnapshotTransaction(
            noncanonical, retainedCatalog, transaction, error),
        "noncanonical scalar fixture must retain exact membership");
    FakeSettings noncanonicalLive;
    noncanonicalLive.values = defaults;
    SettingsSnapshotTransactionResult result = Apply(
        transaction, noncanonicalLive, validateCatalogValue);
    Require(
        !result.succeeded &&
            result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Preflight &&
            noncanonicalLive.readCount == 0u &&
            noncanonicalLive.writeCount == 0u,
        "a valid-domain but noncanonical integer must reject before capture "
        "or mutation");
    Require(
        BuildSettingsSnapshotTransaction(
            nondefault, retainedCatalog, transaction, error),
        "nondefault AO/GI/PT values must form a complete transaction");
    result = Apply(transaction, live);
    Require(
        result.succeeded && !result.rollbackAttempted &&
            result.failureStage ==
                SettingsSnapshotTransactionFailureStage::None &&
            live.values == nondefault,
        "nondefault AO/GI/PT values must apply and verify transactionally");

    const std::size_t writesAfterApply = live.writeCount;
    result = Apply(transaction, live);
    Require(
        result.succeeded && result.changedValueCount == 0u &&
            live.writeCount == writesAfterApply,
        "an idempotent snapshot must verify without issuing a SET");

    Require(
        BuildSettingsSnapshotTransaction(
            defaults, retainedCatalog, transaction, error),
        "captured AO/GI/PT values must form a reverse transaction");
    result = Apply(transaction, live);
    Require(
        result.succeeded && live.values == defaults,
        "nondefault AO/GI/PT values must round-trip to their exact pre-state");

    Require(
        BuildSettingsSnapshotTransaction(
            complete, smallCatalog, transaction, error),
        "dynamic mismatch fixture must build");
    FakeSettings dynamicMismatch;
    dynamicMismatch.values = {
        { "lighting.solution", "ray-marching" },
        { "scene.current", "san-miguel/main.scene.json" },
        { "visibility.ao.enabled", "off" }
    };
    result = Apply(transaction, dynamicMismatch);
    Require(
        !result.succeeded && !result.rollbackAttempted &&
            result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Preflight &&
            dynamicMismatch.writeCount == 0u &&
            dynamicMismatch.readCount == transaction.size() &&
            result.error.find("staged") != std::string::npos,
        "the synchronous compatibility path must reject an unresolved "
        "selector without mutation");

    const std::vector<SettingsSnapshotCatalogEntry> dependentCatalog = {
        { "light.selected", Mode::Selector },
        { "light.selected.flashlight.enabled", Mode::LiveDependent },
        { "light.selected.flashlight.brightness", Mode::LiveDependent }
    };
    const DecodedSettings dependentRequested = {
        { "light.selected", "0:flashlight_1" },
        { "light.selected.flashlight.enabled", "on" },
        { "light.selected.flashlight.brightness", "400" }
    };
    Require(
        BuildSettingsSnapshotTransaction(
            dependentRequested, dependentCatalog, transaction, error),
        "live-dependent fixture must build");
    FakeSettings dependentLive;
    dependentLive.values = {
        { "light.selected", "0:flashlight_1" },
        { "light.selected.flashlight.enabled", "off" },
        { "light.selected.flashlight.brightness", "100" }
    };
    result = Apply(transaction, dependentLive, validateCatalogValue);
    Require(
        result.succeeded && result.changedValueCount == 2u &&
            dependentLive.values == dependentRequested,
        "available dependent boolean and numeric values must apply through "
        "ordinary setters");
    const std::size_t dependentWrites = dependentLive.writeCount;
    result = Apply(transaction, dependentLive, validateCatalogValue);
    Require(
        result.succeeded && result.changedValueCount == 0u &&
            dependentLive.writeCount == dependentWrites,
        "an available dependent transaction must be idempotent");

    DecodedSettings unavailableDependent = dependentRequested;
    unavailableDependent["light.selected.flashlight.enabled"] =
        "<unavailable>";
    unavailableDependent["light.selected.flashlight.brightness"] =
        "<unavailable>";
    Require(
        BuildSettingsSnapshotTransaction(
            unavailableDependent,
            dependentCatalog,
            transaction,
            error),
        "unavailable dependent fixture must build");
    FakeSettings unavailableDependentLive;
    unavailableDependentLive.values = unavailableDependent;
    result = Apply(
        transaction,
        unavailableDependentLive,
        validateCatalogValue);
    Require(
        result.succeeded && result.changedValueCount == 0u &&
            unavailableDependentLive.writeCount == 0u,
        "requested and live unavailable dependent values must be no-ops");

    FakeSettings requestedRealLiveUnavailable;
    requestedRealLiveUnavailable.values = unavailableDependent;
    Require(
        BuildSettingsSnapshotTransaction(
            dependentRequested, dependentCatalog, transaction, error),
        "requested-real fixture must build");
    result = Apply(
        transaction,
        requestedRealLiveUnavailable,
        validateCatalogValue);
    Require(
        !result.succeeded &&
            result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Preflight &&
            requestedRealLiveUnavailable.writeCount == 0u &&
            result.error.find("availability") != std::string::npos,
        "a requested real value with unavailable live dependency must "
        "reject before mutation");

    FakeSettings requestedUnavailableLiveReal;
    requestedUnavailableLiveReal.values = dependentRequested;
    Require(
        BuildSettingsSnapshotTransaction(
            unavailableDependent,
            dependentCatalog,
            transaction,
            error),
        "requested-unavailable fixture must build");
    result = Apply(
        transaction,
        requestedUnavailableLiveReal,
        validateCatalogValue);
    Require(
        !result.succeeded &&
            result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Preflight &&
            requestedUnavailableLiveReal.writeCount == 0u &&
            result.error.find("availability") != std::string::npos,
        "a requested unavailable value with available live dependency must "
        "reject before mutation");

    const std::vector<SettingsSnapshotCatalogEntry> rollbackCatalog = {
        { "visibility.quality", Mode::Mutable },
        { "visibility.samples", Mode::Mutable },
        { "visibility.gi.enabled", Mode::Mutable }
    };
    const DecodedSettings rollbackRequested = {
        { "visibility.quality", "custom" },
        { "visibility.samples", "32" },
        { "visibility.gi.enabled", "on" }
    };
    Require(
        BuildSettingsSnapshotTransaction(
            rollbackRequested, rollbackCatalog, transaction, error),
        "late-invalid fixture must build");
    FakeSettings lateInvalid;
    lateInvalid.values = {
        { "visibility.quality", "high" },
        { "visibility.samples", "8" },
        { "visibility.gi.enabled", "off" }
    };
    std::size_t validationCount = 0u;
    result = Apply(
        transaction,
        lateInvalid,
        [&validationCount](
            std::string_view name,
            std::string_view,
            std::string& validationError)
        {
            ++validationCount;
            if (name == "visibility.gi.enabled")
            {
                validationError = "injected late parse failure";
                return false;
            }
            return true;
        });
    Require(
        !result.succeeded &&
            result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Preflight &&
            validationCount == transaction.size() &&
            lateInvalid.readCount == 0u &&
            lateInvalid.writeCount == 0u,
        "a late invalid value must reject the whole payload before capture "
        "or mutation");

    Require(
        BuildSettingsSnapshotTransaction(
            rollbackRequested, rollbackCatalog, transaction, error),
        "rollback fixture must build");
    FakeSettings rollback;
    rollback.values = {
        { "visibility.quality", "high" },
        { "visibility.samples", "8" },
        { "visibility.gi.enabled", "off" }
    };
    const auto rollbackPreState = rollback.values;
    rollback.failOnceOnWrite = "visibility.samples";
    rollback.mutateBeforeFailure = true;
    result = Apply(transaction, rollback);
    Require(
        !result.succeeded && result.rollbackAttempted &&
            result.rollbackSucceeded &&
            result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Apply &&
            rollback.values == rollbackPreState &&
            result.error.find("injected SET failure") != std::string::npos,
        "a partial mid-apply failure must restore and verify the complete "
        "captured pre-state");

    FakeSettings readbackMismatch;
    readbackMismatch.values = rollbackPreState;
    bool ignoredFirstSamplesWrite = false;
    result = ApplySettingsSnapshotTransaction(
        transaction,
        [](std::string_view, std::string_view, std::string&)
        {
            return true;
        },
        [&readbackMismatch](
            std::string_view name,
            std::string& value,
            std::string& readError)
        {
            return readbackMismatch.Read(name, value, readError);
        },
        [&readbackMismatch, &ignoredFirstSamplesWrite](
            std::string_view name,
            std::string_view value,
            std::string& writeError)
        {
            if (name == "visibility.samples" &&
                value == "32" &&
                !ignoredFirstSamplesWrite)
            {
                ignoredFirstSamplesWrite = true;
                ++readbackMismatch.writeCount;
                return true;
            }
            return readbackMismatch.Write(name, value, writeError);
        });
    Require(
        !result.succeeded &&
            result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Readback &&
            result.rollbackAttempted && result.rollbackSucceeded &&
            readbackMismatch.values == rollbackPreState,
        "a writer that reports success without publishing the requested "
        "value must fail readback and restore the pre-state");

    std::map<std::string, std::string, std::less<>> rollbackFailureValues =
        rollbackPreState;
    bool injectedApplyFailure = false;
    result = ApplySettingsSnapshotTransaction(
        transaction,
        [](std::string_view, std::string_view, std::string&)
        {
            return true;
        },
        [&rollbackFailureValues](
            std::string_view name,
            std::string& value,
            std::string& readError)
        {
            const auto found = rollbackFailureValues.find(name);
            if (found == rollbackFailureValues.end())
            {
                readError = "missing rollback-failure value";
                return false;
            }
            value = found->second;
            return true;
        },
        [&rollbackFailureValues, &injectedApplyFailure](
            std::string_view name,
            std::string_view value,
            std::string& writeError)
        {
            if (name == "visibility.samples" && value == "32")
            {
                injectedApplyFailure = true;
                writeError = "injected apply failure";
                return false;
            }
            if (injectedApplyFailure &&
                name == "visibility.quality" && value == "high")
            {
                writeError = "injected rollback failure";
                return false;
            }
            rollbackFailureValues[std::string(name)] = std::string(value);
            return true;
        });
    Require(
        !result.succeeded &&
            result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Apply &&
            result.rollbackAttempted && !result.rollbackSucceeded &&
            result.error.find("rollback failed") != std::string::npos,
        "rollback failure must remain distinct from the triggering apply "
        "failure");

    DecodedSettings unavailable = complete;
    unavailable["visibility.ao.enabled"] = "<unavailable>";
    Require(
        BuildSettingsSnapshotTransaction(
            unavailable, smallCatalog, transaction, error),
        "unavailable fixture must preserve exact membership");
    FakeSettings unavailableLive;
    unavailableLive.values = {
        { "lighting.solution", "ray-marching" },
        { "scene.current", "bistro/main.scene.json" },
        { "visibility.ao.enabled", "off" }
    };
    result = Apply(transaction, unavailableLive);
    Require(
        !result.succeeded && unavailableLive.writeCount == 0u &&
            result.failureStage ==
                SettingsSnapshotTransactionFailureStage::Preflight &&
            result.error.find("unavailable sentinel") != std::string::npos,
        "the unavailable sentinel must never become a mutable SET value");

    std::cout << "UVSR settings snapshot transaction validation passed\n";
    return EXIT_SUCCESS;
}
