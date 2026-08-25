#pragma once

#include <cstdint>
#include <utility>

namespace uvsr
{
    [[nodiscard]] constexpr bool PbrDeferredDispatchIsReady(
        bool hasPipeline,
        bool hasBindingLayout,
        bool hasBindingSet,
        bool hasConstantBuffer,
        bool hasOutput) noexcept
    {
        return hasPipeline && hasBindingLayout && hasBindingSet &&
            hasConstantBuffer && hasOutput;
    }

    enum class PbrDeferredLightingRenderStatus : std::uint8_t
    {
        Failed,
        Dispatched
    };

    struct PbrDeferredLightingRenderResult
    {
        PbrDeferredLightingRenderStatus status =
            PbrDeferredLightingRenderStatus::Failed;
        std::uint32_t dispatchedViewCount = 0u;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == PbrDeferredLightingRenderStatus::Dispatched;
        }
    };

    class PbrDeferredLightingRenderTransaction
    {
    public:
        explicit PbrDeferredLightingRenderTransaction(
            std::uint32_t expectedViewCount) noexcept
            : m_ExpectedViewCount(expectedViewCount)
        {
        }

        void MarkFailed() noexcept
        {
            m_Failed = true;
        }

        void RecordDispatch() noexcept
        {
            if (m_Failed)
                return;
            if (m_DispatchedViewCount >= m_ExpectedViewCount)
            {
                m_Failed = true;
                return;
            }
            ++m_DispatchedViewCount;
        }

        [[nodiscard]] PbrDeferredLightingRenderResult Finish() const noexcept
        {
            const bool complete = !m_Failed && m_ExpectedViewCount > 0u &&
                m_DispatchedViewCount == m_ExpectedViewCount;
            return {
                complete
                    ? PbrDeferredLightingRenderStatus::Dispatched
                    : PbrDeferredLightingRenderStatus::Failed,
                m_DispatchedViewCount
            };
        }

    private:
        std::uint32_t m_ExpectedViewCount = 0u;
        std::uint32_t m_DispatchedViewCount = 0u;
        bool m_Failed = false;
    };

    template <typename Dispatch>
    [[nodiscard]] bool ExecutePbrDeferredLightingView(
        PbrDeferredLightingRenderTransaction& transaction,
        bool prerequisitesReady,
        Dispatch&& dispatch)
    {
        if (!prerequisitesReady)
        {
            transaction.MarkFailed();
            return false;
        }
        std::forward<Dispatch>(dispatch)();
        transaction.RecordDispatch();
        return true;
    }
}
