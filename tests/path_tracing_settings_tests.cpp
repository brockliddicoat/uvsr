#include "path_tracing_settings.h"

#include <iostream>

namespace
{
    bool Require(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }
}

int main()
{
    bool ok = true;
    ok &= Require(
        uvsr::PathTracingBounceCount == 4u &&
            uvsr::PathTracingSamplesPerFrame == 1u &&
            uvsr::PathTracingRussianRouletteStart == 3u,
        "The standard transport recipe must remain fixed and explicit.");
    ok &= Require(
        uvsr::IsValidLightingSolution(
            uvsr::LightingSolution::RayMarching) &&
            uvsr::IsValidLightingSolution(
                uvsr::LightingSolution::PathTracing),
        "Both retained renderer solutions must remain valid.");
    ok &= Require(
        !uvsr::IsValidLightingSolution(
            static_cast<uvsr::LightingSolution>(255u)),
        "Unknown renderer solutions must fail closed.");

    const uvsr::LightingSolutionTransition enterPathTracing =
        uvsr::ResolveLightingSolutionTransition(
            uvsr::LightingSolution::RayMarching,
            uvsr::LightingSolution::PathTracing);
    const uvsr::LightingSolutionTransition reapplyPathTracing =
        uvsr::ResolveLightingSolutionTransition(
            uvsr::LightingSolution::PathTracing,
            uvsr::LightingSolution::PathTracing);
    const uvsr::LightingSolutionTransition rejectUnknown =
        uvsr::ResolveLightingSolutionTransition(
            uvsr::LightingSolution::PathTracing,
            static_cast<uvsr::LightingSolution>(255u));
    ok &= Require(
        enterPathTracing.accepted && enterPathTracing.changed &&
            enterPathTracing.openPathTracingDrawer &&
            enterPathTracing.resetHistory &&
            enterPathTracing.selection ==
                uvsr::LightingSolution::PathTracing &&
            reapplyPathTracing.accepted &&
            !reapplyPathTracing.changed &&
            !reapplyPathTracing.openPathTracingDrawer &&
            reapplyPathTracing.resetHistory &&
            !rejectUnknown.accepted && !rejectUnknown.changed &&
            !rejectUnknown.resetHistory &&
            rejectUnknown.selection ==
                uvsr::LightingSolution::PathTracing,
        "Lighting-solution selection, drawer, or history-reset transition changed.");

    const uvsr::SelectedLightingTransport raster =
        uvsr::ResolveSelectedLightingTransport(
            uvsr::LightingSolution::RayMarching, false, false);
    ok &= Require(
        raster.renderRayMarching && !raster.retainPathTracingSelection &&
            raster.state ==
                uvsr::SelectedLightingTransportState::RayMarching,
        "Ray Marching must select the raster transport.");

    const uvsr::SelectedLightingTransport preparing =
        uvsr::ResolveSelectedLightingTransport(
            uvsr::LightingSolution::PathTracing, false, false);
    const uvsr::SelectedLightingTransport active =
        uvsr::ResolveSelectedLightingTransport(
            uvsr::LightingSolution::PathTracing, true, false);
    const uvsr::SelectedLightingTransport unavailable =
        uvsr::ResolveSelectedLightingTransport(
            uvsr::LightingSolution::PathTracing, false, true);
    ok &= Require(
        !preparing.renderRayMarching &&
            preparing.retainPathTracingSelection &&
            preparing.state ==
                uvsr::SelectedLightingTransportState::PathTracingPreparing &&
            !active.renderRayMarching && active.retainPathTracingSelection &&
            active.state ==
                uvsr::SelectedLightingTransportState::PathTracingActive &&
            !unavailable.renderRayMarching &&
            unavailable.retainPathTracingSelection &&
            unavailable.state ==
                uvsr::SelectedLightingTransportState::PathTracingUnavailable,
        "Path Tracing must stay selected without raster fallback while "
        "preparing, active, or unavailable.");

    const uvsr::PathTracingPipelineResources completeResources = {
        true, true, true, true, true, true
    };
    const uvsr::PathTracingAvailability available =
        uvsr::ResolvePathTracingAvailability(true, completeResources);
    ok &= Require(
        available.rayQuerySupported &&
            available.executablePipelineAvailable,
        "DXR hardware and a complete standard pipeline must be executable.");

    bool uvsr::PathTracingPipelineResources::* const resourceStages[] = {
        &uvsr::PathTracingPipelineResources::bindlessLayout,
        &uvsr::PathTracingPipelineResources::constantBuffer,
        &uvsr::PathTracingPipelineResources::sampler,
        &uvsr::PathTracingPipelineResources::bindingLayout,
        &uvsr::PathTracingPipelineResources::shader,
        &uvsr::PathTracingPipelineResources::pipeline
    };
    for (auto stage : resourceStages)
    {
        uvsr::PathTracingPipelineResources failed = completeResources;
        failed.*stage = false;
        const uvsr::PathTracingAvailability status =
            uvsr::ResolvePathTracingAvailability(true, failed);
        ok &= Require(
            status.rayQuerySupported &&
                !status.executablePipelineAvailable,
            "Injected path-tracing construction failure must not erase "
            "the device's DXR capability.");
    }

    const uvsr::PathTracingAvailability unsupportedHardware =
        uvsr::ResolvePathTracingAvailability(false, completeResources);
    ok &= Require(
        !unsupportedHardware.rayQuerySupported &&
            !unsupportedHardware.executablePipelineAvailable,
        "A complete pipeline must not misreport unsupported DXR hardware.");

    return ok ? 0 : 1;
}
