#pragma once

#include <cstdint>

namespace uvsr
{
    struct VisibilityPipelinePreparationState
    {
        uint32_t completedPipelines = 0u;
        bool failed = false;
    };

    [[nodiscard]] constexpr bool CommitVisibilityPipelineCreation(
        VisibilityPipelinePreparationState& state,
        bool hasShader,
        bool hasBindingLayout,
        bool hasPipeline,
        uint32_t requiredPipelineCount) noexcept
    {
        if (state.failed || state.completedPipelines >= requiredPipelineCount)
            return false;
        if (!hasShader || !hasBindingLayout || !hasPipeline)
        {
            state.failed = true;
            return false;
        }
        ++state.completedPipelines;
        return state.completedPipelines == requiredPipelineCount;
    }

    [[nodiscard]] constexpr bool VisibilityDispatchIsReady(
        bool hasPipeline,
        bool hasBindingSet,
        bool hasResources) noexcept
    {
        return hasPipeline && hasBindingSet && hasResources;
    }
}
