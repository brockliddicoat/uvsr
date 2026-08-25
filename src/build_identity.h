#pragma once

#include "engine_identity.h"

#include <string_view>

namespace uvsr
{
    [[nodiscard]] std::string_view GetBuiltSourceCommit() noexcept;
    [[nodiscard]] std::string_view GetBuiltSourceIdentity() noexcept;
    [[nodiscard]] bool IsBuiltSourceTreeClean() noexcept;
    [[nodiscard]] bool IsBuiltProduction() noexcept;
    [[nodiscard]] std::string_view GetBuiltConfiguration() noexcept;
    [[nodiscard]] std::string_view GetBuiltSettingsNumberHash() noexcept;
    [[nodiscard]] std::string_view GetBuiltEngineVersion() noexcept;
    [[nodiscard]] std::string_view GetBuiltEngineProductVersion() noexcept;
}
