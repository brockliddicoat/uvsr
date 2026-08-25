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
            log::error("%s", text);
            return;
        case nvrhi::MessageSeverity::Fatal:
            log::fatal("%s", text);
        }

        log::error("NVRHI emitted an unknown message severity: %s", text);
    }
}
