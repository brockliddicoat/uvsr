#include "renderer_scene_retirement.h"

#include <chrono>
#include <memory>
#include <utility>

namespace uvsr
{
    RendererSceneRetirement::RendererSceneRetirement(
        nvrhi::IDevice* device)
        : RendererSceneRetirement([device]()
        {
            if (!device)
                return RendererSceneRetirementOperations{};

            struct DeviceQueryState
            {
                nvrhi::DeviceHandle device;
                nvrhi::EventQueryHandle query;
                std::chrono::steady_clock::time_point armedAt;
            };
            auto state = std::make_shared<DeviceQueryState>();
            state->device = device;

            RendererSceneRetirementOperations operations;
            operations.armQuery = [state]()
            {
                if (!state->query)
                    state->query = state->device->createEventQuery();
                if (!state->query)
                    return false;
                state->device->resetEventQuery(state->query);
                state->device->setEventQuery(
                    state->query, nvrhi::CommandQueue::Graphics);
                state->armedAt = std::chrono::steady_clock::now();
                return true;
            };
            operations.pollQuery = [state]()
            {
                if (!state->device || !state->query)
                    return RendererSceneQueryStatus::Failed;
                if (!state->device->pollEventQuery(state->query))
                {
                    if (std::chrono::steady_clock::now() - state->armedAt <
                        std::chrono::seconds(5))
                    {
                        return RendererSceneQueryStatus::Pending;
                    }
                    return state->device->waitForIdle()
                        ? RendererSceneQueryStatus::Complete
                        : RendererSceneQueryStatus::Failed;
                }
                state->device->resetEventQuery(state->query);
                return RendererSceneQueryStatus::Complete;
            };
            operations.waitForIdle = [state]()
            {
                return state->device && state->device->waitForIdle();
            };
            return operations;
        }())
    {
    }

    RendererSceneRetirement::RendererSceneRetirement(
        RendererSceneRetirementOperations operations)
        : m_Operations(std::move(operations))
    {
    }

    bool RendererSceneRetirement::Begin() noexcept
    {
        if (!m_Operations || m_State != State::Idle)
            return false;
        m_State = State::ArmQuery;
        m_UsedBlockingFallback = false;
        return true;
    }

    RendererSceneRetirementStatus RendererSceneRetirement::Poll()
    {
        switch (m_State)
        {
        case State::Idle:
            return RendererSceneRetirementStatus::Idle;

        case State::ArmQuery:
            if (!m_Operations.armQuery())
            {
                // Allocation failure is exceptional. Preserve lifetime
                // correctness with a blocking retirement rather than freeing
                // resources that may still be referenced by the queue.
                m_UsedBlockingFallback = true;
                if (!m_Operations.waitForIdle())
                {
                    m_State = State::Failed;
                    return RendererSceneRetirementStatus::Failed;
                }
                m_State = State::Ready;
                return RendererSceneRetirementStatus::Ready;
            }
            m_State = State::WaitForQuery;
            return RendererSceneRetirementStatus::Pending;

        case State::WaitForQuery:
            switch (m_Operations.pollQuery())
            {
            case RendererSceneQueryStatus::Pending:
                return RendererSceneRetirementStatus::Pending;
            case RendererSceneQueryStatus::Complete:
                m_State = State::Ready;
                return RendererSceneRetirementStatus::Ready;
            case RendererSceneQueryStatus::Failed:
                m_State = State::Failed;
                return RendererSceneRetirementStatus::Failed;
            }
            m_State = State::Failed;
            return RendererSceneRetirementStatus::Failed;

        case State::Ready:
            return RendererSceneRetirementStatus::Ready;
        case State::Failed:
            return RendererSceneRetirementStatus::Failed;
        }
        return RendererSceneRetirementStatus::Idle;
    }

    bool RendererSceneRetirement::Consume() noexcept
    {
        if (m_State != State::Ready)
            return false;
        m_State = State::Idle;
        return true;
    }
}
