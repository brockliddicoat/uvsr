#pragma once

#include "denoiser_backend.h"

#include <memory>

namespace uvsr
{
    [[nodiscard]] bool HasNrdTypedUavReadWriteSupport(
        nvrhi::FormatSupport support) noexcept;

    [[nodiscard]] std::unique_ptr<IDenoiserSignalBackend>
        CreateCompiledNrdDenoiserSignalBackend();
}
