#pragma once

#include <nvrhi/nvrhi.h>
#include <atomic>
#include <stdint.h>

namespace uvsr
{
    class RendererNvrhiMessageCallback final : public nvrhi::IMessageCallback
    {
    public:
        // errors can arrive from resource-creation workers as well as recording.
        [[nodiscard]] uint64_t GetErrorCount() const noexcept
        {
            return m_ErrorCount.load(std::memory_order_relaxed);
        }
        void message(
            nvrhi::MessageSeverity severity,
            const char* messageText) override;
    private:
        std::atomic<uint64_t> m_ErrorCount{0};
    };
}
