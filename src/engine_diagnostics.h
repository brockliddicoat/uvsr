#pragma once

#include "json_output.h"

namespace uvsr
{
    [[nodiscard]] json::EncodedText BuildIdentityJson() noexcept;

    struct EngineDiagnosticCommandResult
    {
        bool handled = false;
        int exitCode = 0;
    };

    [[nodiscard]] EngineDiagnosticCommandResult TryRunEngineDiagnosticCommand(
        int argumentCount,
        const char* const* arguments) noexcept;
}
