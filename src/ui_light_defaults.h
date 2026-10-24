#pragma once

#include "renderer_scene_light.h"
#include <stddef.h>
#include <stdint.h>

namespace uvsr
{
    struct UiLightDefaults
    {
        RendererSceneLightKind type = RendererSceneLightKind::Count;
        RendererSceneLightDirection direction{0.0, -1.0, 0.0};
        gpu_contract::Float3 color{1.f, 1.f, 1.f};
        float irradiance = 1.f;
        float angularSize = 0.f;
        float radius = 0.f;
        float intensity = 1.f;
        float innerAngle = 180.f;
        float outerAngle = 180.f;
    };

    struct UiLightDefaultsKey
    {
        const char* scene = nullptr;
        size_t sceneSize = 0;
        uint32_t ordinal = 0;
        const char* light = nullptr;
        size_t lightSize = 0;
    };

    enum class UiLightDefaultsError { None, InvalidInput, Capacity, Allocation };

    // captures the first value for each serialized scene/ordinal/name key.
    // entries survive scene replacement and die with the UI owner.
    class UiLightDefaultsCache final
    {
    public:
        UiLightDefaultsCache() noexcept = default;
        ~UiLightDefaultsCache() noexcept;
        UiLightDefaultsCache(const UiLightDefaultsCache&) = delete;
        UiLightDefaultsCache& operator=(const UiLightDefaultsCache&) = delete;

        // borrows the key only during this call. failure preserves cache/output.
        [[nodiscard]] bool ReadOrCapture(const UiLightDefaultsKey& key,
            const UiLightDefaults& current, UiLightDefaults& output, UiLightDefaultsError& error) noexcept;
        void Clear() noexcept;
        [[nodiscard]] size_t Count() const noexcept { return m_Count; }
        [[nodiscard]] size_t Capacity() const noexcept { return m_Capacity; }

    private:
        struct Entry;
        Entry** m_Entries = nullptr;
        size_t m_Capacity = 0;
        size_t m_Count = 0;
    };

#if defined(UVSR_UI_LIGHT_DEFAULTS_TEST_HOOKS)
    void FailUiLightDefaultsAllocationAfter(size_t count) noexcept;
    void ClearUiLightDefaultsAllocationFailure() noexcept;
#endif
}
