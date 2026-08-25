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

    // The production tracer has one conventional transport recipe. These are
    // implementation constants, not settings or persistence fields.
    inline constexpr uint32_t PathTracingBounceCount =
        UVSR_PATH_TRACING_BOUNCE_COUNT;
    inline constexpr uint32_t PathTracingSamplesPerFrame =
        UVSR_PATH_TRACING_SAMPLES_PER_FRAME;
    inline constexpr uint32_t PathTracingRussianRouletteStart =
        UVSR_PATH_TRACING_RUSSIAN_ROULETTE_START;

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

    [[nodiscard]] inline constexpr const char* GetLightingSolutionLabel(
        LightingSolution solution) noexcept
    {
        return solution == LightingSolution::RayMarching
            ? "Ray Marching"
            : solution == LightingSolution::PathTracing
                ? "Path Tracing"
                : "";
    }
}
