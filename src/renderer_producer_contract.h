#pragma once

namespace uvsr
{
    struct RendererProducerDispatchContract
    {
        bool directionalRequested = false;
        bool directionalDispatched = false;
        bool flashlightRequested = false;
        bool flashlightDispatched = false;
        bool skyRequested = false;
        bool skyDispatched = false;

        [[nodiscard]] constexpr bool IsComplete() const noexcept
        {
            return (!directionalRequested || directionalDispatched) &&
                (!flashlightRequested || flashlightDispatched) &&
                (!skyRequested || skyDispatched);
        }
    };

}
