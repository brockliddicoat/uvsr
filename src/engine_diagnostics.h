#pragma once

#include <optional>
#include <string>

namespace uvsr
{
    [[nodiscard]] std::string BuildIdentityJson();
    [[nodiscard]] std::string BuildSettingsContractJson();

    [[nodiscard]] std::optional<int> TryRunEngineDiagnosticCommand(
        int argumentCount,
        const char* const* arguments);
}
