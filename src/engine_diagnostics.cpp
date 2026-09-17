#include "engine_diagnostics.h"
#include "build_identity.h"
#include "settings_snapshot_schema.h"

#include <stdio.h>
#include <string.h>

namespace uvsr
{
    namespace
    {
        json::TextView Text(std::string_view value) noexcept { return {value.data(), value.size()}; }

        bool EmitIdentity(json::OutputWriter& output, const void*) noexcept
        {
            return output.Raw("{\"executable\":\"uvsr-engine.exe\",\"source_commit\":") &&
                output.String(Text(GetBuiltSourceCommit())) && output.Raw(",\"source_identity\":") &&
                output.String(Text(GetBuiltSourceIdentity())) && output.Raw(",\"source_tree_clean\":") &&
                output.Boolean(IsBuiltSourceTreeClean()) && output.Raw(",\"production\":") &&
                output.Boolean(IsBuiltProduction()) && output.Raw(",\"configuration\":") &&
                output.String(Text(GetBuiltConfiguration())) && output.Raw(",\"settings_hash\":") &&
                output.String(Text(GetBuiltSettingsNumberHash())) && output.Raw(",\"engine_version\":") &&
                output.String(Text(GetBuiltEngineVersion())) && output.Raw(",\"product_version\":") &&
                output.String(Text(GetBuiltEngineProductVersion())) && output.Raw("}\n");
        }
    }

    json::EncodedText BuildIdentityJson() noexcept { return json::EncodedText(EmitIdentity); }

    EngineDiagnosticCommandResult TryRunEngineDiagnosticCommand(int argumentCount, const char* const* arguments) noexcept
    {
        if (argumentCount != 2) return {};
        json::EncodedText output;
        if (strcmp(arguments[1], "--identity-json") == 0) output = BuildIdentityJson();
        else if (strcmp(arguments[1], "--settings-contract-json") == 0) output = BuildSettingsContractJson();
        else return {};
        if (!output.IsValid())
        {
            fprintf(stderr, "UVSR diagnostic JSON failed: %s\n", output.Failure().message);
            return {true, 1};
        }
        const size_t written = fwrite(output.Data(), 1, output.Size(), stdout);
        const bool flushed = fflush(stdout) == 0;
        return {true, written == output.Size() && flushed ? 0 : 1};
    }
}
