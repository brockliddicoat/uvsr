#include "renderer_scene_retirement.h"


namespace uvsr
{
    RendererSceneRetirement::RendererSceneRetirement(
        RendererSceneRetirementOperations operations)
        : m_Operations(operations)
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

    RendererSceneRetirementStatus
        RendererSceneRetirement::CompleteBlocking()
    {
        if (m_State == State::Idle || !m_Operations)
            return RendererSceneRetirementStatus::Idle;
        if (m_State == State::Ready)
            return RendererSceneRetirementStatus::Ready;

        m_UsedBlockingFallback = true;
        if (!m_Operations.waitForIdle(m_Operations.context))
        {
            m_State = State::Failed;
            return RendererSceneRetirementStatus::Failed;
        }
        m_State = State::Ready;
        return RendererSceneRetirementStatus::Ready;
    }

    RendererSceneRetirementStatus RendererSceneRetirement::Poll()
    {
        switch (m_State)
        {
        case State::Idle:
            return RendererSceneRetirementStatus::Idle;

        case State::ArmQuery:
            if (!m_Operations.armQuery(m_Operations.context))
            {
                // query creation or recording failed. keep the scene until
                // blocking completion supplies a valid proof.
                return CompleteBlocking();
            }
            m_State = State::WaitForQuery;
            return RendererSceneRetirementStatus::Pending;

        case State::WaitForQuery:
            switch (m_Operations.pollQuery(m_Operations.context))
            {
            case RendererSceneQueryStatus::Pending:
                return RendererSceneRetirementStatus::Pending;
            case RendererSceneQueryStatus::Complete:
                m_State = State::Ready;
                return RendererSceneRetirementStatus::Ready;
            case RendererSceneQueryStatus::Failed:
                return CompleteBlocking();
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
