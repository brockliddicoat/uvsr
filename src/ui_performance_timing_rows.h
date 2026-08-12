#pragma once

#include <cstdint>
#include <unordered_set>

namespace uvsr
{
    enum class PerformanceTimingRowPresentation
    {
        Hidden,
        Measurement,
        Unavailable
    };

    struct PerformanceTimingRowState
    {
        PerformanceTimingRowPresentation presentation =
            PerformanceTimingRowPresentation::Hidden;
        double milliseconds = 0.0;

        [[nodiscard]] bool IsVisible() const
        {
            return presentation != PerformanceTimingRowPresentation::Hidden;
        }

        [[nodiscard]] bool HasMeasurement() const
        {
            return presentation ==
                PerformanceTimingRowPresentation::Measurement;
        }
    };

    class PerformanceTimingRowRetention
    {
    public:
        [[nodiscard]] PerformanceTimingRowState Resolve(
            std::uint32_t viewId,
            std::uint32_t rowId,
            double milliseconds,
            bool available)
        {
            const std::uint64_t key =
                (static_cast<std::uint64_t>(viewId) << 32u) |
                static_cast<std::uint64_t>(rowId);

            if (available)
                m_SeenRows.insert(key);

            if (m_SeenRows.find(key) == m_SeenRows.end())
                return {};

            if (!available)
            {
                return {
                    PerformanceTimingRowPresentation::Unavailable,
                    0.0
                };
            }

            return {
                PerformanceTimingRowPresentation::Measurement,
                milliseconds
            };
        }

    private:
        std::unordered_set<std::uint64_t> m_SeenRows;
    };
}
