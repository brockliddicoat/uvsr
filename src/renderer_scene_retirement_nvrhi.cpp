#include "renderer_scene_retirement_nvrhi.h"
#include <Windows.h>

namespace uvsr
{
    RendererSceneRetirementNvrhi::RendererSceneRetirementNvrhi(nvrhi::IDevice* device)
        : m_Device(device)
        , m_Gate(device ? RendererSceneRetirementOperations{ this, Arm, PollQuery, WaitForIdle }
                        : RendererSceneRetirementOperations{})
    {
    }

    bool RendererSceneRetirementNvrhi::Arm(void* context)
    {
        auto& state = *static_cast<RendererSceneRetirementNvrhi*>(context);
        if (!state.m_Query)
            state.m_Query = state.m_Device->createEventQuery();
        if (!state.m_Query)
            return false;
        state.m_Device->resetEventQuery(state.m_Query);
        state.m_Device->setEventQuery(state.m_Query, nvrhi::CommandQueue::Graphics);
        state.m_ArmedAtMilliseconds = GetTickCount64();
        return true;
    }

    RendererSceneQueryStatus RendererSceneRetirementNvrhi::PollQuery(void* context)
    {
        auto& state = *static_cast<RendererSceneRetirementNvrhi*>(context);
        if (!state.m_Device || !state.m_Query)
            return RendererSceneQueryStatus::Failed;
        if (!state.m_Device->pollEventQuery(state.m_Query))
            return GetTickCount64() - state.m_ArmedAtMilliseconds < 5000
                ? RendererSceneQueryStatus::Pending : RendererSceneQueryStatus::Failed;
        state.m_Device->resetEventQuery(state.m_Query);
        return RendererSceneQueryStatus::Complete;
    }

    bool RendererSceneRetirementNvrhi::WaitForIdle(void* context)
    {
        auto& state = *static_cast<RendererSceneRetirementNvrhi*>(context);
        return state.m_Device && state.m_Device->waitForIdle();
    }
}
