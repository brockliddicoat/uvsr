#pragma once

#include <stdint.h>

namespace uvsr
{
    // this counts recorded dispatches, not asynchronous per-pixel accepted samples.
    inline constexpr uint32_t RuntimeCapturePathDispatchTarget = 5u;

    class RuntimeCaptureSequence final
    {
    public:
        [[nodiscard]] bool Arm(uint32_t pathDispatchTarget, bool skyRequired,
            bool directionalRequired = false, bool flashlightRequired = false) noexcept
        {
            if (m_Pending || (pathDispatchTarget != 0u &&
                (skyRequired || directionalRequired || flashlightRequired)))
                return Fail();
            m_PathDispatchTarget = pathDispatchTarget;
            m_PathDispatchCount = 0u;
            m_RasterRequiredMask = (skyRequired ? 1u : 0u) |
                (directionalRequired ? 2u : 0u) | (flashlightRequired ? 4u : 0u);
            m_RasterProducerMask = 0u;
            m_SkySamplePhase = 0u;
            m_DirectionalSamplePhase = 0u;
            m_FlashlightSamplePhase = 0u;
            m_Ready = false;
            m_Pending = true;
            return true;
        }

        [[nodiscard]] bool ObservePath(bool dispatched, bool historyReset) noexcept
        {
            if (!m_Pending || m_PathDispatchTarget == 0u)
                return Fail();
            if (!dispatched)
                return historyReset ? Fail() : true;
            if ((m_PathDispatchCount == 0u) != historyReset)
                return Fail();
            ++m_PathDispatchCount;
            if (m_PathDispatchCount == m_PathDispatchTarget)
            {
                m_Pending = false;
                m_Ready = true;
            }
            return true;
        }

        [[nodiscard]] bool ObserveRaster(bool skyDispatched, uint32_t skyPhase,
            bool directionalDispatched, uint32_t directionalPhase,
            bool flashlightDispatched, uint32_t flashlightPhase) noexcept
        {
            const uint32_t actualMask = (skyDispatched ? 1u : 0u) |
                (directionalDispatched ? 2u : 0u) | (flashlightDispatched ? 4u : 0u);
            if (!m_Pending || m_PathDispatchTarget != 0u ||
                actualMask != m_RasterRequiredMask ||
                (skyDispatched && skyPhase != SamplePhase()) ||
                (directionalDispatched && directionalPhase != SamplePhase()) ||
                (flashlightDispatched && flashlightPhase != SamplePhase()))
                return Fail();
            m_RasterProducerMask = actualMask;
            m_SkySamplePhase = skyDispatched ? skyPhase : 0u;
            m_DirectionalSamplePhase = directionalDispatched ? directionalPhase : 0u;
            m_FlashlightSamplePhase = flashlightDispatched ? flashlightPhase : 0u;
            m_Pending = false;
            m_Ready = true;
            return true;
        }

        [[nodiscard]] bool IsReady() const noexcept { return m_Ready; }
        [[nodiscard]] uint32_t PathDispatchCount() const noexcept { return m_PathDispatchCount; }
        // mask bits describe actual dispatches: sky, directional, flashlight.
        [[nodiscard]] uint32_t RasterProducerMask() const noexcept { return m_RasterProducerMask; }
        [[nodiscard]] bool SkySamplePhaseValid() const noexcept { return (m_RasterProducerMask & 1u) != 0u; }
        [[nodiscard]] uint32_t SkySamplePhase() const noexcept { return m_SkySamplePhase; }
        [[nodiscard]] uint32_t DirectionalSamplePhase() const noexcept { return m_DirectionalSamplePhase; }
        [[nodiscard]] uint32_t FlashlightSamplePhase() const noexcept { return m_FlashlightSamplePhase; }
        [[nodiscard]] static constexpr uint32_t SamplePhase() noexcept { return 0u; }

    private:
        [[nodiscard]] bool Fail() noexcept
        {
            m_Pending = false;
            m_Ready = false;
            return false;
        }

        uint32_t m_PathDispatchTarget = 0u;
        uint32_t m_PathDispatchCount = 0u;
        uint32_t m_RasterRequiredMask = 0u;
        uint32_t m_RasterProducerMask = 0u;
        uint32_t m_SkySamplePhase = 0u;
        uint32_t m_DirectionalSamplePhase = 0u;
        uint32_t m_FlashlightSamplePhase = 0u;
        bool m_Pending = false;
        bool m_Ready = false;
    };
}
