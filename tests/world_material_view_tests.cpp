#include "world_material_view.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "World material view validation failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }
}

int main()
{
    using namespace uvsr;

    Require(
        SelectableWorldMaterialViews.size() == 5,
        "the dropdown should expose four White World views and one GI view");
    Require(
        SelectableWorldMaterialViews.back() ==
            WorldMaterialView::IndirectDiffuseResponse,
        "Indirect Diffuse Response should be the final dropdown entry");
    Require(
        std::string(GetWorldMaterialViewLabel(
            WorldMaterialView::IndirectDiffuseResponse)) ==
            "Indirect Diffuse Response",
        "the GI-only view should use the material-response label");

    constexpr WorldMaterialViewAvailability available{};
    constexpr WorldMaterialViewState responseState =
        MakeWorldMaterialViewState(
            WorldMaterialView::IndirectDiffuseResponse);
    Require(
        responseState.whiteWorldMode == 0u &&
            responseState.showIndirectDiffuseOnly,
        "the indirect response view should disable White World and enable GI-only composition");
    Require(
        ResolveWorldMaterialView(responseState, available) ==
            WorldMaterialView::IndirectDiffuseResponse,
        "an active GI response view should resolve to its dropdown entry");

    for (WorldMaterialView view : SelectableWorldMaterialViews)
    {
        if (view == WorldMaterialView::IndirectDiffuseResponse)
            continue;

        const WorldMaterialViewState state = MakeWorldMaterialViewState(view);
        Require(
            !state.showIndirectDiffuseOnly,
            "every White World view should exit GI-only composition");
        Require(
            ResolveWorldMaterialView(state, available) == view,
            "each White World view should round-trip through dropdown state");
    }

    WorldMaterialViewAvailability unavailable = available;
    unavailable.pbrEnabled = false;
    Require(
        ResolveWorldMaterialView(responseState, unavailable) ==
            WorldMaterialView::WhiteWorldOff,
        "disabling PBR should exit the indirect response view");
    unavailable = available;
    unavailable.deferredShading = false;
    Require(
        ResolveWorldMaterialView(responseState, unavailable) ==
            WorldMaterialView::WhiteWorldOff,
        "leaving deferred shading should exit the indirect response view");
    unavailable = available;
    unavailable.visibilityEnabled = false;
    Require(
        ResolveWorldMaterialView(responseState, unavailable) ==
            WorldMaterialView::WhiteWorldOff,
        "disabling visibility should exit the indirect response view");
    unavailable = available;
    unavailable.indirectDiffuseActive = false;
    Require(
        ResolveWorldMaterialView(responseState, unavailable) ==
            WorldMaterialView::WhiteWorldOff,
        "disabling effective GI should exit the indirect response view");
    const WorldMaterialViewState normalizedState =
        NormalizeWorldMaterialViewState(responseState, unavailable);
    Require(
        !normalizedState.showIndirectDiffuseOnly,
        "fallback should clear the stored GI-only request");
    Require(
        ResolveWorldMaterialView(normalizedState, available) ==
            WorldMaterialView::WhiteWorldOff,
        "restoring GI should not silently resume an exited response view");

    Require(
        ResolveWorldMaterialView({ 99u, false }, available) ==
            WorldMaterialView::WhiteWorldOff,
        "an invalid White World value should fall back safely");

    std::cout << "World material view validation passed\n";
    return EXIT_SUCCESS;
}
