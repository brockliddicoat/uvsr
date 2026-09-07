#include "engine_diagnostics.h"

#include "build_identity.h"
#include "json_document.h"
#include "settings_snapshot_schema.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace uvsr
{
    std::string BuildIdentityJson()
    {
        return
            "{\"executable\":\"uvsr-engine.exe\","
            "\"source_commit\":\"" +
            json::Escape(GetBuiltSourceCommit()) +
            "\",\"source_identity\":\"" +
            json::Escape(GetBuiltSourceIdentity()) +
            "\",\"source_tree_clean\":" +
            (IsBuiltSourceTreeClean() ? "true" : "false") +
            ",\"production\":" +
            (IsBuiltProduction() ? "true" : "false") +
            ",\"configuration\":\"" +
            json::Escape(GetBuiltConfiguration()) +
            "\",\"settings_hash\":\"" +
            json::Escape(GetBuiltSettingsNumberHash()) +
            "\",\"engine_version\":\"" +
            json::Escape(GetBuiltEngineVersion()) +
            "\",\"product_version\":\"" +
            json::Escape(GetBuiltEngineProductVersion()) + "\"}\n";
    }

    std::optional<int> TryRunEngineDiagnosticCommand(
        int argumentCount,
        const char* const* arguments)
    {
        if (argumentCount != 2)
        {
            return std::nullopt;
        }

        std::string json;
        if (std::strcmp(arguments[1], "--identity-json") == 0)
        {
            json = BuildIdentityJson();
        }
        else if (std::strcmp(
                arguments[1],
                "--settings-contract-json") == 0)
        {
            json = BuildSettingsContractJson();
        }
        else
        {
            return std::nullopt;
        }
        const std::size_t written = std::fwrite(
            json.data(),
            1u,
            json.size(),
            stdout);
        std::fflush(stdout);
        return written == json.size() ? 0 : 1;
    }
}
