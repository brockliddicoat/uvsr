#pragma once

#include <array>
#include <cstdint>

namespace uvsr
{
    enum class WorldMaterialView : uint8_t
    {
        WhiteWorldOff,
        WhiteWorldOn,
        WhiteWorldPreserveNormals,
        WhiteWorldPreserveEmissives,
        IndirectDiffuseResponse,
        Count
    };

    inline constexpr std::array<WorldMaterialView, 5>
        SelectableWorldMaterialViews = {
            WorldMaterialView::WhiteWorldOff,
            WorldMaterialView::WhiteWorldOn,
            WorldMaterialView::WhiteWorldPreserveNormals,
            WorldMaterialView::WhiteWorldPreserveEmissives,
            WorldMaterialView::IndirectDiffuseResponse
        };

    [[nodiscard]] constexpr const char* GetWorldMaterialViewLabel(
        WorldMaterialView view)
    {
        switch (view)
        {
        case WorldMaterialView::WhiteWorldOff:
            return "White World Off";
        case WorldMaterialView::WhiteWorldOn:
            return "White World On";
        case WorldMaterialView::WhiteWorldPreserveNormals:
            return "White World Preserve Normals";
        case WorldMaterialView::WhiteWorldPreserveEmissives:
            return "White World Preserve Emissives";
        case WorldMaterialView::IndirectDiffuseResponse:
            return "Indirect Diffuse Response";
        case WorldMaterialView::Count:
            return "Unavailable";
        }

        return "Unavailable";
    }

    struct WorldMaterialViewState
    {
        uint32_t whiteWorldMode = 0;
        bool showIndirectDiffuseOnly = false;
    };

    struct WorldMaterialViewAvailability
    {
        bool pbrEnabled = true;
        bool deferredShading = true;
        bool visibilityEnabled = true;
        bool indirectDiffuseActive = true;
    };

    [[nodiscard]] constexpr bool IsWorldMaterialViewAvailable(
        WorldMaterialView view,
        WorldMaterialViewAvailability availability)
    {
        if (view != WorldMaterialView::IndirectDiffuseResponse)
            return view != WorldMaterialView::Count;

        return availability.pbrEnabled &&
            availability.deferredShading &&
            availability.visibilityEnabled &&
            availability.indirectDiffuseActive;
    }

    [[nodiscard]] constexpr WorldMaterialView ResolveWorldMaterialView(
        WorldMaterialViewState state,
        WorldMaterialViewAvailability availability)
    {
        if (state.showIndirectDiffuseOnly &&
            IsWorldMaterialViewAvailable(
                WorldMaterialView::IndirectDiffuseResponse,
                availability))
        {
            return WorldMaterialView::IndirectDiffuseResponse;
        }

        switch (state.whiteWorldMode)
        {
        case 0:
            return WorldMaterialView::WhiteWorldOff;
        case 1:
            return WorldMaterialView::WhiteWorldOn;
        case 2:
            return WorldMaterialView::WhiteWorldPreserveNormals;
        case 3:
            return WorldMaterialView::WhiteWorldPreserveEmissives;
        default:
            return WorldMaterialView::WhiteWorldOff;
        }
    }

    [[nodiscard]] constexpr WorldMaterialViewState NormalizeWorldMaterialViewState(
        WorldMaterialViewState state,
        WorldMaterialViewAvailability availability)
    {
        if (state.showIndirectDiffuseOnly &&
            !IsWorldMaterialViewAvailable(
                WorldMaterialView::IndirectDiffuseResponse,
                availability))
        {
            state.showIndirectDiffuseOnly = false;
        }

        return state;
    }

    [[nodiscard]] constexpr WorldMaterialViewState MakeWorldMaterialViewState(
        WorldMaterialView view)
    {
        if (view == WorldMaterialView::IndirectDiffuseResponse)
            return { 0u, true };

        const uint32_t whiteWorldMode = view < WorldMaterialView::Count
            ? uint32_t(view)
            : 0u;
        return { whiteWorldMode, false };
    }
}
