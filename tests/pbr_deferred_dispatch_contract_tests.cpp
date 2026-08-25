#include "pbr_deferred_dispatch_contract.h"

#include <cstdlib>
#include <iostream>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "PBR deferred dispatch contract failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using uvsr::PbrDeferredDispatchIsReady;
    Require(PbrDeferredDispatchIsReady(true, true, true, true, true),
        "complete production dispatch state was rejected");
    Require(!PbrDeferredDispatchIsReady(false, true, true, true, true),
        "null pipeline was dispatchable");
    Require(!PbrDeferredDispatchIsReady(true, false, true, true, true),
        "null binding layout was dispatchable");
    Require(!PbrDeferredDispatchIsReady(true, true, false, true, true),
        "injected binding-set allocation failure was dispatchable");
    Require(!PbrDeferredDispatchIsReady(true, true, true, false, true),
        "null constant buffer was dispatchable");
    Require(!PbrDeferredDispatchIsReady(true, true, true, true, false),
        "null output was dispatchable");
    return EXIT_SUCCESS;
}
