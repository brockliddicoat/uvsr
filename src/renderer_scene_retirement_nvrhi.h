#pragma once

#include "renderer_scene_retirement.h"
#include <nvrhi/nvrhi.h>

namespace uvsr
{
    // private backend owner. ordinary resource retention stays in NVRHI; this
    // gate protects mutable scene descriptor slots that NVRHI cannot retain.
    class RendererSceneRetirementNvrhi final
    {
    public:
        explicit RendererSceneRetirementNvrhi(nvrhi::IDevice* device);
        RendererSceneRetirementNvrhi(const RendererSceneRetirementNvrhi&) = delete;
        RendererSceneRetirementNvrhi& operator=(const RendererSceneRetirementNvrhi&) = delete;

        [[nodiscard]] bool IsValid() const noexcept { return m_Gate.IsValid(); }
        [[nodiscard]] bool Begin() noexcept { return m_Gate.Begin(); }
        [[nodiscard]] RendererSceneRetirementStatus Poll() { return m_Gate.Poll(); }
        [[nodiscard]] RendererSceneRetirementStatus CompleteBlocking() { return m_Gate.CompleteBlocking(); }
        [[nodiscard]] bool Consume() noexcept { return m_Gate.Consume(); }
        [[nodiscard]] bool UsedBlockingFallback() const noexcept { return m_Gate.UsedBlockingFallback(); }

    private:
        static bool Arm(void* context);
        static RendererSceneQueryStatus PollQuery(void* context);
        static bool WaitForIdle(void* context);

        nvrhi::DeviceHandle m_Device;
        nvrhi::EventQueryHandle m_Query;
        uint64_t m_ArmedAtMilliseconds = 0;
        RendererSceneRetirement m_Gate;
    };
}
