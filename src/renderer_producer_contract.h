#pragma once

#include <type_traits>
#include <utility>

namespace uvsr
{
    struct RendererProducerDispatchContract
    {
        bool screenSpaceRequested = false;
        bool screenSpaceDispatched = false;
        bool directionalRequested = false;
        bool directionalDispatched = false;
        bool flashlightRequested = false;
        bool flashlightDispatched = false;
        bool skyRequested = false;
        bool skyDispatched = false;

        [[nodiscard]] constexpr bool IsComplete() const noexcept
        {
            return (!screenSpaceRequested || screenSpaceDispatched) &&
                (!directionalRequested || directionalDispatched) &&
                (!flashlightRequested || flashlightDispatched) &&
                (!skyRequested || skyDispatched);
        }
    };

    template<class Cancel>
    class PreparedRendererTransactionCancellation final
    {
    public:
        PreparedRendererTransactionCancellation(
            bool& prepared,
            Cancel cancel) noexcept(
                std::is_nothrow_move_constructible_v<Cancel>)
            : m_Prepared(&prepared)
            , m_Cancel(std::move(cancel))
        {
        }

        PreparedRendererTransactionCancellation(
            const PreparedRendererTransactionCancellation&) = delete;
        PreparedRendererTransactionCancellation& operator=(
            const PreparedRendererTransactionCancellation&) = delete;
        PreparedRendererTransactionCancellation(
            PreparedRendererTransactionCancellation&&) = delete;
        PreparedRendererTransactionCancellation& operator=(
            PreparedRendererTransactionCancellation&&) = delete;

        ~PreparedRendererTransactionCancellation() noexcept
        {
            if (*m_Prepared)
            {
                m_Cancel();
                *m_Prepared = false;
            }
        }

    private:
        bool* m_Prepared;
        Cancel m_Cancel;
    };

    template<class Cancel>
    [[nodiscard]] auto MakePreparedRendererTransactionCancellation(
        bool& prepared,
        Cancel&& cancel)
    {
        using StoredCancel = std::decay_t<Cancel>;
        return PreparedRendererTransactionCancellation<StoredCancel>(
            prepared,
            std::forward<Cancel>(cancel));
    }
}
