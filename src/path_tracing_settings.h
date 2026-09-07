#pragma once

#include "path_tracing_transport_contract.h"

#include <cstdint>

namespace uvsr
{
    enum class LightingSolution : uint8_t
    {
        RayMarching,
        PathTracing
    };

    inline constexpr uint32_t PathTracingSamplesPerFrame =
        UVSR_PATH_TRACING_SAMPLES_PER_FRAME;

    struct PathTracingSettings
    {
        int maximumBounces = 30;
        int minimumBounces = 2;
        bool fireflyFilter = true;
        float fireflyThreshold = 50.f;
    };
    inline constexpr PathTracingSettings DefaultPathTracingSettings;

    [[nodiscard]] inline bool IsValidPathTracingSettings(
        const PathTracingSettings& settings) noexcept
    {
        return settings.maximumBounces >= 1 && settings.maximumBounces <= 30 &&
            settings.minimumBounces >= 1 && settings.minimumBounces <= settings.maximumBounces &&
            ShaderIsFinite(settings.fireflyThreshold) &&
            settings.fireflyThreshold >= 10.f && settings.fireflyThreshold <= 1.e6f;
    }

    struct PathTracingPipelineResources
    {
        bool bindlessLayout = false;
        bool constantBuffer = false;
        bool sampler = false;
        bool bindingLayout = false;
        bool shader = false;
        bool pipeline = false;

        [[nodiscard]] constexpr bool AreExecutable() const noexcept
        {
            return bindlessLayout && constantBuffer && sampler &&
                bindingLayout && shader && pipeline;
        }
    };

    struct PathTracingAvailability
    {
        bool rayQuerySupported = false;
        bool executablePipelineAvailable = false;
    };

    [[nodiscard]] inline constexpr PathTracingAvailability
    ResolvePathTracingAvailability(
        bool rayQuerySupported,
        const PathTracingPipelineResources& resources) noexcept
    {
        return {
            rayQuerySupported,
            rayQuerySupported && resources.AreExecutable()
        };
    }

    enum class SelectedLightingTransportState : uint8_t
    {
        RayMarching,
        PathTracingPreparing,
        PathTracingActive,
        PathTracingUnavailable
    };

    struct SelectedLightingTransport
    {
        SelectedLightingTransportState state =
            SelectedLightingTransportState::RayMarching;
        bool renderRayMarching = true;
        bool retainPathTracingSelection = false;
    };

    [[nodiscard]] inline constexpr SelectedLightingTransport
    ResolveSelectedLightingTransport(
        LightingSolution selection,
        bool pathTransportActive,
        bool pathTransportUnavailable) noexcept
    {
        if (selection != LightingSolution::PathTracing)
            return {};
        return {
            pathTransportActive
                ? SelectedLightingTransportState::PathTracingActive
                : pathTransportUnavailable
                    ? SelectedLightingTransportState::PathTracingUnavailable
                    : SelectedLightingTransportState::PathTracingPreparing,
            false,
            true
        };
    }

    [[nodiscard]] inline constexpr bool IsValidLightingSolution(
        LightingSolution solution) noexcept
    {
        return solution == LightingSolution::RayMarching ||
            solution == LightingSolution::PathTracing;
    }

    struct LightingSolutionTransition
    {
        LightingSolution selection = LightingSolution::RayMarching;
        bool accepted = false;
        bool changed = false;
        bool openPathTracingDrawer = false;
        bool resetHistory = false;
    };

    [[nodiscard]] inline constexpr LightingSolutionTransition
        ResolveLightingSolutionTransition(
            LightingSolution previous,
            LightingSolution requested) noexcept
    {
        if (!IsValidLightingSolution(requested))
            return { previous, false, false, false, false };
        const bool changed = previous != requested;
        return {
            requested,
            true,
            changed,
            changed && requested == LightingSolution::PathTracing,
            true
        };
    }

}
