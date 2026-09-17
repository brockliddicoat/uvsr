#include "renderer_nvrhi_message_callback.h"

#include "renderer_log.h"

namespace uvsr
{
    void RendererNvrhiMessageCallback::message(
        nvrhi::MessageSeverity severity,
        const char* messageText)
    {
        const char* text = messageText ? messageText : "";
        switch (severity)
        {
        case nvrhi::MessageSeverity::Info:
            log::info("%s", text);
            return;
        case nvrhi::MessageSeverity::Warning:
            log::warning("%s", text);
            return;
        case nvrhi::MessageSeverity::Error:
            m_ErrorCount.fetch_add(1, std::memory_order_relaxed);
            log::error("%s", text);
            return;
        case nvrhi::MessageSeverity::Fatal:
            m_ErrorCount.fetch_add(1, std::memory_order_relaxed);
            log::fatal("%s", text);
        }

        m_ErrorCount.fetch_add(1, std::memory_order_relaxed);
        log::error("NVRHI emitted an unknown message severity: %s", text);
    }
}
