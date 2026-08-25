#pragma once

#include <nvrhi/nvrhi.h>

namespace uvsr
{
    class RendererNvrhiMessageCallback final : public nvrhi::IMessageCallback
    {
    public:
        void message(
            nvrhi::MessageSeverity severity,
            const char* messageText) override;
    };
}
