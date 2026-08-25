#include "engine_diagnostics.h"
#include "build_identity.h"
#include "settings_snapshot.h"
#include "settings_snapshot_schema.h"
#include "ui_settings_command_catalog.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Engine diagnostics validation failed: "
                      << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    const char* identityArguments[] = { "uvsr-engine.exe", "--identity-json" };
    const char* settingsArguments[] = {
        "uvsr-engine.exe", "--settings-contract-json"
    };
    const char* ordinaryArguments[] = { "uvsr-engine.exe", "-width", "1280" };
    const std::optional<int> identity =
        uvsr::TryRunEngineDiagnosticCommand(2, identityArguments);
    const std::optional<int> ordinary =
        uvsr::TryRunEngineDiagnosticCommand(3, ordinaryArguments);
    const std::optional<int> settings =
        uvsr::TryRunEngineDiagnosticCommand(2, settingsArguments);
    Require(identity && *identity == 0,
        "identity command did not complete successfully");
    Require(!ordinary, "ordinary renderer arguments were consumed");
    Require(settings && *settings == 0,
        "settings-contract command did not complete successfully");

    const std::string identityJson = uvsr::BuildIdentityJson();
    Require(identityJson.find(
                "\"source_commit\":\"" +
                std::string(uvsr::GetBuiltSourceCommit()) + "\"") !=
            std::string::npos &&
            identityJson.find(
                "\"source_identity\":\"" +
                std::string(uvsr::GetBuiltSourceIdentity()) + "\"") !=
            std::string::npos &&
            identityJson.find(
                std::string("\"source_tree_clean\":") +
                (uvsr::IsBuiltSourceTreeClean() ? "true" : "false")) !=
            std::string::npos &&
            identityJson.find(
                std::string("\"production\":") +
                (uvsr::IsBuiltProduction() ? "true" : "false")) !=
            std::string::npos &&
            identityJson.find(
                "\"configuration\":\"" +
                std::string(uvsr::GetBuiltConfiguration()) + "\"") !=
            std::string::npos,
        "identity JSON omitted exact source identity or build mode");

    const std::string contract = uvsr::BuildSettingsContractJson();
    Require(contract.find("{\"schemaVersion\":7,") == 0u,
        "settings contract did not publish schema 7 first");
    Require(contract.find(
        "\"settingsHash\":\"9c50b0f1515e89d856c8ebb627b86984\"") !=
            std::string::npos,
        "settings contract did not publish the fixed settings hash");
    Require(contract.find(
        "\"engineVersion\":\"40016.45297.20830.35288\"") !=
            std::string::npos,
        "settings contract did not publish the derived engine version");
    Require(contract.find("\"serializationPolicy\":") !=
            std::string::npos,
        "settings contract omitted its authoritative serialization policy");
    Require(contract.find("\"entries\":[") != std::string::npos &&
            contract.find("\"settings\":[") == std::string::npos,
        "settings contract must use the canonical entries key");

    std::vector<const uvsr::UiSettingsCommandDefinition*> values;
    for (const auto& definition : uvsr::UiSettingsCommandCatalog)
    {
        if (definition.kind != uvsr::UiSettingsCommandKind::Action)
            values.push_back(&definition);
    }
    std::sort(
        values.begin(),
        values.end(),
        [](const auto* left, const auto* right)
        {
            return left->name < right->name;
        });
    Require(values.size() == 176u,
        "settings contract value-definition count drifted");
    std::size_t previousPosition = 0u;
    for (const auto* definition : values)
    {
        const std::string needle =
            "{\"name\":\"" + std::string(definition->name) + "\"";
        const std::size_t position = contract.find(needle);
        Require(position != std::string::npos &&
                position >= previousPosition,
            "settings contract entries were absent or not name-sorted");
        previousPosition = position;
    }
    Require(contract.find("ui.settings-collapsed") != std::string::npos &&
            contract.find("material-editor.visible") != std::string::npos &&
            contract.find("\"persistence\":\"SessionOnly\","
                "\"snapshotMember\":false") != std::string::npos &&
            contract.find("reset-settings") == std::string::npos,
        "full value persistence membership or action exclusion drifted");
    return EXIT_SUCCESS;
}
