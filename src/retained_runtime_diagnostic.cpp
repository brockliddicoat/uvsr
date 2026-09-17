#include "retained_runtime_diagnostic.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <new>
#include <type_traits>
#include <stdlib.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_RETAINED_CASE_TEST_HOOKS)
        thread_local size_t caseAllocationsLeft = SIZE_MAX;
#endif
        bool CaseStorageFailure(SettingsSnapshotError& error,
            SettingsSnapshotErrorCode code, const char* message) noexcept
        {
            error = {};
            error.code = code;
            error.message = message;
            return false;
        }

        template<class T> bool GrowCaseStorage(T*& entries, size_t count,
            size_t& capacity, size_t requested, T* appended,
            SettingsSnapshotError& error) noexcept
        {
            static_assert(std::is_nothrow_move_constructible_v<T> &&
                std::is_nothrow_move_assignable_v<T> && std::is_nothrow_destructible_v<T>);
            static_assert(alignof(T) <= alignof(std::max_align_t));
            if (requested > size_t(PTRDIFF_MAX) / sizeof(T))
                return CaseStorageFailure(error, SettingsSnapshotErrorCode::Capacity,
                    "retained runtime storage exceeds addressable capacity");
#if defined(UVSR_RETAINED_CASE_TEST_HOOKS)
            if (!caseAllocationsLeft)
                return CaseStorageFailure(error, SettingsSnapshotErrorCode::OutOfMemory,
                    "cannot allocate retained runtime storage");
            if (caseAllocationsLeft != SIZE_MAX) --caseAllocationsLeft;
#endif
            T* candidate = static_cast<T*>(malloc(requested * sizeof(T)));
            if (!candidate)
                return CaseStorageFailure(error, SettingsSnapshotErrorCode::OutOfMemory,
                    "cannot allocate retained runtime storage");
            // append first because its source may be a record in the old array.
            if (appended) new (candidate + count) T(std::move(*appended));
            for (size_t index = 0; index < count; ++index)
            {
                new (candidate + index) T(std::move(entries[index]));
                entries[index].~T();
            }
            free(entries);
            entries = candidate;
            capacity = requested;
            return true;
        }
    }

    template<class T> void RetainedRuntimeList<T>::Clear() noexcept
    {
        for (size_t index = 0; index < m_Count; ++index) m_Entries[index].~T();
        free(m_Entries);
        m_Entries = nullptr;
        m_Count = m_Capacity = 0;
    }
    template<class T> RetainedRuntimeList<T>::~RetainedRuntimeList() noexcept { Clear(); }
    template<class T> RetainedRuntimeList<T>::RetainedRuntimeList(RetainedRuntimeList&& other) noexcept
    {
        *this = std::move(other);
    }
    template<class T> RetainedRuntimeList<T>& RetainedRuntimeList<T>::operator=(RetainedRuntimeList&& other) noexcept
    {
        if (this == &other) return *this;
        Clear();
        m_Entries = other.m_Entries;
        m_Count = other.m_Count;
        m_Capacity = other.m_Capacity;
        other.m_Entries = nullptr;
        other.m_Count = other.m_Capacity = 0;
        return *this;
    }
    template<class T> bool RetainedRuntimeList<T>::Reserve(size_t capacity, SettingsSnapshotError& error) noexcept
    {
        error = {};
        return capacity <= m_Capacity || GrowCaseStorage(m_Entries, m_Count,
            m_Capacity, capacity, static_cast<T*>(nullptr), error);
    }
    template<class T> bool RetainedRuntimeList<T>::Append(T&& value, SettingsSnapshotError& error) noexcept
    {
        error = {};
        if (m_Count == m_Capacity)
        {
            constexpr size_t Maximum = size_t(PTRDIFF_MAX) / sizeof(T);
            if (m_Count == Maximum)
                return CaseStorageFailure(error, SettingsSnapshotErrorCode::Capacity,
                    "retained runtime storage exceeds addressable capacity");
            const size_t capacity = m_Capacity > Maximum / 2 ? Maximum :
                (m_Capacity ? m_Capacity * 2 : 4);
            if (!GrowCaseStorage(m_Entries, m_Count, m_Capacity, capacity, &value, error)) return false;
        }
        else new (m_Entries + m_Count) T(std::move(value));
        ++m_Count;
        return true;
    }
    template class RetainedRuntimeList<RetainedRuntimeSetting>;
    template class RetainedRuntimeList<RetainedRuntimeCase>;

#if defined(UVSR_RETAINED_CASE_TEST_HOOKS)
    void FailRetainedCaseAllocationAfter(size_t successfulAllocations) noexcept { caseAllocationsLeft = successfulAllocations; }
    void ClearRetainedCaseAllocationFailure() noexcept { caseAllocationsLeft = SIZE_MAX; }
#endif


    namespace
    {
        [[nodiscard]] bool DomainTokenValue(
            SettingId id,
            std::size_t tokenIndex,
            UiSettingsValue& value,
            SettingsSnapshotError& error)
        {
            const UiSettingsCommandDefinition* definition =
                FindSettingsCommandDefinition(id);
            if (!definition ||
                tokenIndex >= definition->typedDomain.tokenCount)
            {
                error = {};
                error.code = SettingsSnapshotErrorCode::Catalog;
                error.message = "retained runtime case has an invalid domain token";
                return false;
            }
            return value.SetToken(definition->typedDomain.tokens[tokenIndex], error);
        }

        [[nodiscard]] RetainedRuntimeCase::Setting BooleanSetting(
            SettingId id, bool value)
        {
            return { id, UiSettingsValue::Boolean(value) };
        }

        [[nodiscard]] RetainedRuntimeCase::Setting FloatSetting(
            SettingId id, float value)
        {
            return { id, UiSettingsValue::Float(value) };
        }

        [[nodiscard]] bool Set(
            RetainedRuntimeCase& runtimeCase,
            RetainedRuntimeCase::Setting setting, SettingsSnapshotError& error) noexcept
        {
            const auto existing = std::find_if(
                runtimeCase.settings.begin(),
                runtimeCase.settings.end(),
                [id = setting.id](const auto& candidate)
                {
                    return candidate.id == id;
                });
            if (existing != runtimeCase.settings.end())
                existing->value = std::move(setting.value);
            else
                return runtimeCase.settings.Append(std::move(setting), error);
            return true;
        }

        [[nodiscard]] bool SetToken(RetainedRuntimeCase& runtimeCase,
            SettingId id, std::size_t tokenIndex, SettingsSnapshotError& error)
        {
            RetainedRuntimeCase::Setting setting;
            setting.id = id;
            if (!DomainTokenValue(id, tokenIndex, setting.value, error)) return false;
            return Set(runtimeCase, std::move(setting), error);
        }

        [[nodiscard]] bool SetSelector(RetainedRuntimeCase& runtimeCase,
            SettingId id, std::string_view text, SettingsSnapshotError& error)
        {
            RetainedRuntimeCase::Setting setting;
            setting.id = id;
            if (!setting.value.SetSelector(text, error)) return false;
            return Set(runtimeCase, std::move(setting), error);
        }

        [[nodiscard]] const UiSettingsValue* Get(
            const RetainedRuntimeCase& runtimeCase,
            SettingId id)
        {
            const auto setting = std::find_if(
                runtimeCase.settings.begin(),
                runtimeCase.settings.end(),
                [id](const auto& candidate)
                {
                    return candidate.id == id;
                });
            return setting == runtimeCase.settings.end()
                ? nullptr
                : &setting->value;
        }

        [[nodiscard]] bool IsBoolean(
            const UiSettingsValue* value, bool expected) noexcept
        {
            return value && value->kind == UiSettingsValueKind::Boolean &&
                value->boolean == expected;
        }

        [[nodiscard]] bool Raster(std::initializer_list<std::string_view> name,
            RetainedRuntimeCase& runtimeCase, SettingsSnapshotError& error)
        {
            if (!runtimeCase.name.AssignParts(name, error)) return false;
            if (!SetToken(runtimeCase, SettingId::LightingSolution, 0u, error)) return false;
            if (!Set(runtimeCase, BooleanSetting(SettingId::RepresentationAllowRayTraversal, true), error)) return false;
            if (!Set(runtimeCase, BooleanSetting(SettingId::SkyVisibilityEnabled, false), error)) return false;
            if (!SetSelector(runtimeCase, SettingId::LightSelected, "flashlight_1", error)) return false;
            if (!Set(runtimeCase, BooleanSetting(SettingId::LightSelectedFlashlightEnabled, false), error)) return false;
            if (!Set(runtimeCase, BooleanSetting(SettingId::LightSelectedFlashlightCastShadows, true), error)) return false;
            if (!Set(runtimeCase, BooleanSetting(SettingId::ShadowsRayTracedEnabled, true), error)) return false;
            return true;
        }

        [[nodiscard]] bool ContainsLoweredSceneText(
            std::string_view haystack, std::string_view needle) noexcept
        {
            if (needle.empty()) return true;
            if (needle.size() > haystack.size()) return false;
            const std::size_t last = haystack.size() - needle.size();
            for (std::size_t start = 0; start <= last; ++start)
            {
                std::size_t offset = 0;
                while (offset < needle.size() &&
                    static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[start + offset]))) == needle[offset])
                    ++offset;
                if (offset == needle.size()) return true;
            }
            return false;
        }

        constexpr double MaximumBaselineFrameMilliseconds = 1000.0;
        constexpr double FrameTimingRelativeTolerance = 4.0;
        constexpr double FrameTimingAdditiveToleranceMilliseconds = 50.0;
        constexpr auto RetainedRuntimeGlobalTimeout =
            std::chrono::hours(6);

        [[nodiscard]] std::size_t ActionCount(
            const RetainedRuntimeCase& runtimeCase) noexcept
        {
            if (runtimeCase.exerciseRetainedStateChanges)
                return 3u;
            return runtimeCase.action == RetainedRuntimeAction::None
                ? 0u
                : 1u;
        }

        [[nodiscard]] RetainedRuntimeAction ActionAt(
            const RetainedRuntimeCase& runtimeCase,
            std::size_t index) noexcept
        {
            if (!runtimeCase.exerciseRetainedStateChanges)
            {
                return index == 0u
                    ? runtimeCase.action
                    : RetainedRuntimeAction::None;
            }
            constexpr std::array Actions = {
                RetainedRuntimeAction::NudgeCamera,
                RetainedRuntimeAction::ChangeScene,
                RetainedRuntimeAction::ResizeViewport
            };
            return index < Actions.size()
                ? Actions[index]
                : RetainedRuntimeAction::None;
        }


    }

    template <typename Scalar, typename Decode>
    RuntimeOutputEvidence AnalyzeRuntimeLinearRgba(
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowPitchBytes,
        Decode decode)
    {
        RuntimeOutputEvidence evidence;
        evidence.width = width;
        evidence.height = height;
        if (!pixels || width == 0u || height == 0u ||
            rowPitchBytes < std::size_t(width) * sizeof(Scalar) * 4u)
        {
            return evidence;
        }

        float minimum = std::numeric_limits<float>::infinity();
        float maximum = -std::numeric_limits<float>::infinity();
        double luminanceSum = 0.0;
        double luminanceSquareSum = 0.0;
        double horizontalGradientSum = 0.0;
        std::uint64_t luminanceCount = 0u;
        std::uint64_t horizontalGradientCount = 0u;
        std::array<Scalar, 3> firstRgb{};
        bool haveFirst = false;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const auto* row = reinterpret_cast<const Scalar*>(
                static_cast<const unsigned char*>(pixels) +
                std::size_t(y) * rowPitchBytes);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const Scalar* rgba = row + std::size_t(x) * 4u;
                if (!haveFirst)
                {
                    firstRgb = { rgba[0], rgba[1], rgba[2] };
                    haveFirst = true;
                }
                else if (rgba[0] != firstRgb[0] ||
                    rgba[1] != firstRgb[1] || rgba[2] != firstRgb[2])
                {
                    ++evidence.varyingPixelCount;
                }

                float rgb[3]{};
                bool rgbFinite = true;
                for (std::size_t component = 0u; component < 4u;
                    ++component)
                {
                    const auto* componentBytes =
                        reinterpret_cast<const unsigned char*>(
                            &rgba[component]);
                    for (std::size_t byte = 0u; byte < sizeof(Scalar); ++byte)
                    {
                        evidence.linearHash ^= componentBytes[byte];
                        evidence.linearHash *= 1099511628211ull;
                    }
                    const float value = decode(rgba[component]);
                    if (!std::isfinite(value))
                    {
                        ++evidence.nonFiniteComponentCount;
                        if (component < 3u)
                            rgbFinite = false;
                        continue;
                    }
                    ++evidence.finiteComponentCount;
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                    if (component < 3u)
                        rgb[component] = value;
                }
                if (rgbFinite)
                {
                    const double luminance =
                        0.2126 * double(rgb[0]) +
                        0.7152 * double(rgb[1]) +
                        0.0722 * double(rgb[2]);
                    luminanceSum += luminance;
                    luminanceSquareSum += luminance * luminance;
                    const double mappedLuminance = std::sqrt(std::clamp(
                        luminance / 4.0, 0.0, 1.0));
                    const std::size_t histogramBin = std::min<std::size_t>(
                        evidence.linearLuminanceHistogram.size() - 1u,
                        static_cast<std::size_t>(mappedLuminance *
                            double(evidence.linearLuminanceHistogram.size())));
                    ++evidence.linearLuminanceHistogram[histogramBin];
                    ++luminanceCount;
                }
                if (x > 0u && rgbFinite)
                {
                    const Scalar* left = rgba - 4u;
                    const float leftRgb[3] = {
                        decode(left[0]),
                        decode(left[1]),
                        decode(left[2])
                    };
                    const float difference =
                        std::fabs(rgb[0] - leftRgb[0]) +
                        std::fabs(rgb[1] - leftRgb[1]) +
                        std::fabs(rgb[2] - leftRgb[2]);
                    if (std::isfinite(difference) && difference > 0.001f)
                        ++evidence.edgePixelCount;
                    if (std::isfinite(difference))
                    {
                        horizontalGradientSum += double(difference);
                        ++horizontalGradientCount;
                    }
                }
            }
        }
        evidence.minimumLinearValue = std::isfinite(minimum) ? minimum : 0.f;
        evidence.maximumLinearValue = std::isfinite(maximum) ? maximum : 0.f;
        if (luminanceCount > 0u)
        {
            evidence.meanLinearLuminance =
                luminanceSum / double(luminanceCount);
            evidence.rmsLinearLuminance = std::sqrt(
                luminanceSquareSum / double(luminanceCount));
        }
        if (horizontalGradientCount > 0u)
        {
            evidence.meanLinearHorizontalGradient =
                horizontalGradientSum / double(horizontalGradientCount);
        }
        evidence.linearReadbackValid =
            evidence.nonFiniteComponentCount == 0u &&
            evidence.finiteComponentCount ==
                std::uint64_t(width) * std::uint64_t(height) * 4u &&
            evidence.varyingPixelCount > 0u &&
            evidence.edgePixelCount > 0u &&
            evidence.maximumLinearValue > evidence.minimumLinearValue;
        return evidence;
    }

    RuntimeOutputEvidence AnalyzeRuntimeLinearRgba16(
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowPitchBytes)
    {
        const auto decodeHalf = [](std::uint16_t bits)
        {
            const bool negative = (bits & 0x8000u) != 0u;
            const std::uint32_t exponent = (bits >> 10u) & 0x1fu;
            const std::uint32_t mantissa = bits & 0x3ffu;
            float value = 0.f;
            if (exponent == 0u)
            {
                value = mantissa == 0u
                    ? 0.f
                    : std::ldexp(static_cast<float>(mantissa), -24);
            }
            else if (exponent == 0x1fu)
            {
                value = mantissa == 0u
                    ? std::numeric_limits<float>::infinity()
                    : std::numeric_limits<float>::quiet_NaN();
            }
            else
            {
                value = std::ldexp(
                    1.f + static_cast<float>(mantissa) / 1024.f,
                    static_cast<int>(exponent) - 15);
            }
            return negative ? -value : value;
        };
        return AnalyzeRuntimeLinearRgba<std::uint16_t>(
            pixels, width, height, rowPitchBytes, decodeHalf);
    }

    RuntimeOutputEvidence AnalyzeRuntimeLinearRgba32(
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowPitchBytes)
    {
        const auto decodeFloat = [](float value) { return value; };
        return AnalyzeRuntimeLinearRgba<float>(
            pixels, width, height, rowPitchBytes, decodeFloat);
    }

    bool BuildRetainedRuntimeCases(
        std::string_view bistroScene,
        std::string_view sanMiguelScene,
        RetainedRuntimeCases& output,
        SettingsSnapshotError& error) noexcept
    {
        error = {};
        constexpr std::string_view BistroToken =
            "bistro_interior_retextured";
        constexpr std::string_view SanMiguelToken =
            "san_miguel_retextured";

        RetainedRuntimeCases cases;
        if (!cases.Reserve(34u, error)) return false;

        const auto setScene = [&error](
            RetainedRuntimeCase& runtimeCase,
            std::string_view scene,
            std::string_view token)
        {
            if (!SetSelector(runtimeCase, SettingId::SceneCurrent, scene, error)) return false;
            if (!runtimeCase.expectedSceneToken.Assign(token, error)) return false;
            return true;
        };

        struct DiscreteSpec
        {
            std::string_view name;
            SettingId id;
            int tokenIndex = -1;
            bool boolean = false;
        };
        const std::array<DiscreteSpec, 7> DiscreteCases = {{
            { "noise-pattern-spatial-white",
                SettingId::NoisePattern, 0 },
            { "noise-pattern-spatial-blue",
                SettingId::NoisePattern, 1 },
            { "noise-resolution-64x64",
                SettingId::NoiseResolution, 0 },
            { "noise-resolution-256x256",
                SettingId::NoiseResolution, 2 },
            { "noise-resolution-512x512",
                SettingId::NoiseResolution, 3 },
            { "noise-animate-off",
                SettingId::NoiseAnimateSamples, -1, false },
            { "noise-accumulate-on",
                SettingId::NoiseAccumulateSamples, -1, true },
        }};
        for (std::size_t index = 0u; index < DiscreteCases.size(); ++index)
        {
            const DiscreteSpec& spec = DiscreteCases[index];
            RetainedRuntimeCase runtimeCase;
            if (!Raster({spec.name}, runtimeCase, error) ||
                !SetToken(runtimeCase, SettingId::NoisePattern, 2u, error) ||
                !SetToken(runtimeCase, SettingId::NoiseResolution, 1u, error)) return false;
            if (!Set(runtimeCase, BooleanSetting(SettingId::NoiseAnimateSamples, true), error)) return false;
            if (!Set(runtimeCase, BooleanSetting(SettingId::NoiseAccumulateSamples, false), error)) return false;
            runtimeCase.expectDirectionalVisibility = true;
            runtimeCase.assertLightingAccumulationState = true;
            runtimeCase.expectLightingAccumulation =
                spec.id == SettingId::NoiseAccumulateSamples;
            if (spec.tokenIndex >= 0)
            {
                if (!SetToken(runtimeCase, spec.id, std::size_t(spec.tokenIndex), error)) return false;
            }
            else
                if (!Set(runtimeCase, BooleanSetting(spec.id, spec.boolean), error)) return false;
            if ((index % 2u) == 0u)
            {
                if (!setScene(runtimeCase, bistroScene, BistroToken)) return false;
            }
            else
            {
                if (!setScene(runtimeCase, sanMiguelScene, SanMiguelToken)) return false;
            }
            if (!cases.Append(std::move(runtimeCase), error)) return false;
        }

        struct AllSignalSpec
        {
            std::string_view name;
            bool startsInBistro;
        };
        constexpr std::array<AllSignalSpec, 2> AllSignalCases = {{
            { "all-signal-bistro-to-san-miguel", true },
            { "all-signal-san-miguel-to-bistro", false }
        }};
        for (const AllSignalSpec& spec : AllSignalCases)
        {
            RetainedRuntimeCase runtimeCase;
            if (!Raster({spec.name}, runtimeCase, error)) return false;
            const std::string_view initialScene = spec.startsInBistro
                ? bistroScene
                : sanMiguelScene;
            const std::string_view finalScene = spec.startsInBistro
                ? sanMiguelScene
                : bistroScene;
            runtimeCase.snapshotRoundTrip = true;
            runtimeCase.exerciseRetainedStateChanges = true;
            runtimeCase.resizeWidth = 704;
            runtimeCase.resizeHeight = 400;
            if (!runtimeCase.actionBaselineSceneToken.Assign(spec.startsInBistro
                ? BistroToken
                : SanMiguelToken, error)) return false;
            if (!runtimeCase.expectedSceneToken.Assign(spec.startsInBistro
                ? SanMiguelToken
                : BistroToken, error)) return false;
            runtimeCase.actionSettingId = SettingId::SceneCurrent;
            if (!runtimeCase.actionBaselineValue.SetSelector(initialScene, error) ||
                !runtimeCase.actionValue.SetSelector(finalScene, error) ||
                !SetSelector(runtimeCase, SettingId::SceneCurrent, initialScene, error)) return false;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilityEnabled, true), error)) return false;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilityDiffuseIbl, true), error)) return false;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilitySpecularIbl, true), error)) return false;
            if (!SetToken(runtimeCase, SettingId::SkyVisibilitySamplesPerPixel, 3u, error) ||
                !SetSelector(runtimeCase, SettingId::LightSelected, "flashlight_1", error)) return false;
            if (!Set(runtimeCase, BooleanSetting(
                SettingId::LightSelectedFlashlightEnabled, true), error)) return false;
            if (!Set(runtimeCase, BooleanSetting(
                SettingId::LightSelectedFlashlightCastShadows, true), error)) return false;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::ShadowsRayTracedEnabled, true), error)) return false;
            runtimeCase.expectDirectionalVisibility = true;
            runtimeCase.expectSkyVisibility = true;
            runtimeCase.expectFlashlightLightingSubmitted = true;
            runtimeCase.assertFlashlightLightingState = true;
            runtimeCase.expectFlashlightVisibility = true;
            runtimeCase.assertFlashlightVisibilityState = true;
            if (!cases.Append(std::move(runtimeCase), error)) return false;
        }

        constexpr std::size_t EnvironmentCount = 6;
        constexpr std::array<float, 3> Compensation = { -18.f, 0.f, 8.f };
        constexpr std::array<float, 3> Brightening = { 0.f, 8.f, 16.f };
        constexpr std::array<float, 3> Darkening = { 16.f, 8.f, 0.f };
        constexpr std::array<float, 3> AdjustmentPeriod = {
            0.05f, 0.2f, 5.f
        };
        for (std::size_t index = 0u; index < EnvironmentCount; ++index)
        {
            RetainedRuntimeCase runtimeCase;
            if (!DomainTokenValue(SettingId::SkyEnvironment, index, runtimeCase.actionValue, error) ||
                !Raster({"hdr-environment-", runtimeCase.actionValue.Text()}, runtimeCase, error) ||
                !setScene(runtimeCase,
                (index % 2u) == 0u ? bistroScene : sanMiguelScene,
                (index % 2u) == 0u ? BistroToken : SanMiguelToken) ||
                !SetToken(runtimeCase, SettingId::SkyEnvironment, index, error)) return false;
            runtimeCase.action = RetainedRuntimeAction::ChangeSetting;
            runtimeCase.actionSettingId = SettingId::SkyEnvironment;
            if (!DomainTokenValue(SettingId::SkyEnvironment,
                (index + EnvironmentCount - 1u) % EnvironmentCount,
                runtimeCase.actionBaselineValue, error)) return false;
            runtimeCase.requireActionOutputDifference = true;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::SkyDiffuseIbl, true), error)) return false;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::SkySpecularIbl, true), error)) return false;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::SkyEnvironmentBackground, true), error)) return false;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilityEnabled, true), error)) return false;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilityDiffuseIbl, true), error)) return false;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilitySpecularIbl, true), error)) return false;
            if (!SetToken(runtimeCase, SettingId::SkyVisibilitySamplesPerPixel, 2u, error)) return false;
            const bool automaticExposure = (index % 2u) != 0u;
            if (!Set(runtimeCase, BooleanSetting(
                SettingId::SkyAutoExposureEnabled, automaticExposure), error)) return false;
            if (automaticExposure)
            {
                const std::size_t profile = index / 2u;
                if (!Set(runtimeCase, FloatSetting(
                    SettingId::SkyAutoExposureExposureCompensation,
                    Compensation[profile]), error)) return false;
                if (!Set(runtimeCase, FloatSetting(
                    SettingId::SkyAutoExposureMaximumBrightening,
                    Brightening[profile]), error)) return false;
                if (!Set(runtimeCase, FloatSetting(
                    SettingId::SkyAutoExposureMaximumDarkening,
                    Darkening[profile]), error)) return false;
                if (!Set(runtimeCase, FloatSetting(
                    SettingId::SkyAutoExposureAdjustmentPeriod,
                    AdjustmentPeriod[profile]), error)) return false;
            }
            runtimeCase.expectSkyVisibility = true;
            runtimeCase.expectAutoExposure = automaticExposure;
            runtimeCase.assertAutoExposureState = true;
            if (!cases.Append(std::move(runtimeCase), error)) return false;
        }

        RetainedRuntimeCase flashlightLighting;
        if (!Raster({"flashlight-lighting-toggle-bistro-1x"}, flashlightLighting, error) ||
            !setScene(flashlightLighting, bistroScene, BistroToken)) return false;
        if (!Set(flashlightLighting, BooleanSetting(
            SettingId::LightSelectedFlashlightEnabled, true), error)) return false;
        if (!Set(flashlightLighting, BooleanSetting(
            SettingId::LightSelectedFlashlightCastShadows, false), error)) return false;
        flashlightLighting.expectFlashlightLightingSubmitted = true;
        flashlightLighting.assertFlashlightLightingState = true;
        flashlightLighting.assertFlashlightVisibilityState = true;
        flashlightLighting.action = RetainedRuntimeAction::ChangeSetting;
        flashlightLighting.actionSettingId =
            SettingId::LightSelectedFlashlightEnabled;
        flashlightLighting.actionBaselineValue =
            UiSettingsValue::Boolean(true);
        flashlightLighting.actionValue = UiSettingsValue::Boolean(false);
        flashlightLighting.requireActionOutputDifference = true;
        if (!cases.Append(std::move(flashlightLighting), error)) return false;

        RetainedRuntimeCase flashlightShadow;
        if (!Raster({"flashlight-shadow-toggle-san-miguel"}, flashlightShadow, error) ||
            !setScene(flashlightShadow, sanMiguelScene, SanMiguelToken)) return false;
        if (!Set(flashlightShadow, BooleanSetting(
            SettingId::LightSelectedFlashlightEnabled, true), error)) return false;
        if (!Set(flashlightShadow, BooleanSetting(
            SettingId::LightSelectedFlashlightCastShadows, true), error)) return false;
        flashlightShadow.expectFlashlightLightingSubmitted = true;
        flashlightShadow.assertFlashlightLightingState = true;
        flashlightShadow.expectFlashlightVisibility = true;
        flashlightShadow.assertFlashlightVisibilityState = true;
        flashlightShadow.action = RetainedRuntimeAction::ChangeSetting;
        flashlightShadow.actionSettingId =
            SettingId::LightSelectedFlashlightCastShadows;
        flashlightShadow.actionBaselineValue =
            UiSettingsValue::Boolean(true);
        flashlightShadow.actionValue = UiSettingsValue::Boolean(false);
        flashlightShadow.requireActionOutputDifference = true;
        if (!cases.Append(std::move(flashlightShadow), error)) return false;

        const auto pathCase = [&bistroScene, &error](std::string_view name,
            RetainedRuntimeCase& runtimeCase)
        {
            if (!Raster({name}, runtimeCase, error)) return false;
            runtimeCase.expectedPathHistoryCount = 3u;
            if (!SetSelector(runtimeCase, SettingId::SceneCurrent, bistroScene, error)) return false;
            if (!Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilityEnabled, false), error)) return false;
            if (!SetToken(runtimeCase, SettingId::LightingSolution, 1u, error) ||
                !SetToken(runtimeCase, SettingId::NoisePattern, 2u, error)) return false;
            if (!runtimeCase.expectedSceneToken.Assign("bistro_interior_retextured", error)) return false;
            return true;
        };

        RetainedRuntimeCase pathBaseline;
        if (!pathCase("path-tracing-bistro", pathBaseline)) return false;
        pathBaseline.snapshotRoundTrip = true;
        if (!cases.Append(std::move(pathBaseline), error)) return false;

        RetainedRuntimeCase pathCamera;
        if (!pathCase("path-history-camera-reset", pathCamera)) return false;
        pathCamera.action = RetainedRuntimeAction::NudgeCamera;
        pathCamera.requirePathHistoryRestart = true;
        if (!cases.Append(std::move(pathCamera), error)) return false;

        RetainedRuntimeCase pathResize;
        if (!pathCase("path-history-resize-reset", pathResize)) return false;
        pathResize.action = RetainedRuntimeAction::ResizeViewport;
        pathResize.resizeWidth = 800;
        pathResize.resizeHeight = 448;
        pathResize.requirePathHistoryRestart = true;
        if (!cases.Append(std::move(pathResize), error)) return false;

        RetainedRuntimeCase pathScene;
        if (!pathCase("path-tracing-san-miguel-scene-reset", pathScene)) return false;
        pathScene.action = RetainedRuntimeAction::ChangeScene;
        pathScene.actionSettingId = SettingId::SceneCurrent;
        if (!pathScene.actionBaselineValue.SetSelector(bistroScene, error)) return false;
        if (!pathScene.actionBaselineSceneToken.Assign(BistroToken, error)) return false;
        if (!pathScene.actionValue.SetSelector(sanMiguelScene, error)) return false;
        if (!pathScene.expectedSceneToken.Assign(SanMiguelToken, error)) return false;
        pathScene.requirePathHistoryRestart = true;
        if (!cases.Append(std::move(pathScene), error)) return false;

        const auto appendPathRestart = [
            &cases, &pathCase, &error](
                std::string_view name,
                RetainedRuntimeAction action,
                SettingId settingId = SettingId::Invalid,
                UiSettingsValue baselineValue = {},
                UiSettingsValue actionValue = {},
                bool requireOutputDifference = false)
        {
            RetainedRuntimeCase runtimeCase;
            if (!pathCase(name, runtimeCase)) return false;
            runtimeCase.action = action;
            runtimeCase.actionSettingId = settingId;
            runtimeCase.actionBaselineValue = std::move(baselineValue);
            runtimeCase.actionValue = std::move(actionValue);
            if (runtimeCase.action == RetainedRuntimeAction::ChangeSetting)
            {
                RetainedRuntimeCase::Setting setting;
                setting.id = runtimeCase.actionSettingId;
                if (!runtimeCase.actionBaselineValue.CloneTo(setting.value, error)) return false;
                if (!Set(runtimeCase, std::move(setting), error)) return false;
            }
            runtimeCase.requirePathHistoryRestart = true;
            runtimeCase.requireActionOutputDifference =
                requireOutputDifference;
            if (!cases.Append(std::move(runtimeCase), error)) return false;
            return true;
        };
        const auto appendTokenPathRestart = [&](std::string_view name, SettingId id,
            std::size_t baselineIndex, std::size_t actionIndex, bool requireDifference = false)
        {
            UiSettingsValue baselineValue;
            UiSettingsValue actionValue;
            return DomainTokenValue(id, baselineIndex, baselineValue, error) &&
                DomainTokenValue(id, actionIndex, actionValue, error) &&
                appendPathRestart(name, RetainedRuntimeAction::ChangeSetting, id,
                    std::move(baselineValue), std::move(actionValue), requireDifference);
        };
        if (!appendTokenPathRestart(
            "path-history-environment-reset",
            SettingId::SkyEnvironment,
            0u, 3u, true)) return false;
        if (!appendPathRestart(
            "path-history-exposure-reset",
            RetainedRuntimeAction::ChangeSetting,
            SettingId::SkyExposure,
            UiSettingsValue::Float(-2.75f),
            UiSettingsValue::Float(-1.75f), true)) return false;
        if (!appendTokenPathRestart(
            "path-history-global-noise-reset",
            SettingId::NoisePattern,
            2u, 1u)) return false;
        if (!appendPathRestart(
            "path-history-material-reset",
            RetainedRuntimeAction::ChangeMaterial)) return false;
        cases.Back().requireActionOutputDifference = true;
        if (!appendPathRestart(
            "path-history-light-reset",
            RetainedRuntimeAction::ChangeLight)) return false;
        if (!appendPathRestart(
            "path-history-flashlight-reset",
            RetainedRuntimeAction::ToggleFlashlight)) return false;
        if (!appendPathRestart(
            "path-history-lighting-solution-cycle",
            RetainedRuntimeAction::CycleLightingSolution)) return false;

        if (!appendPathRestart("path-history-maximum-bounces-reset",
            RetainedRuntimeAction::ChangeSetting, SettingId::PathingMaximumBounces,
            UiSettingsValue::Integer(1), UiSettingsValue::Integer(8), true)) return false;
        if (!appendPathRestart("path-history-minimum-bounces-reset",
            RetainedRuntimeAction::ChangeSetting, SettingId::PathingMinimumBounces,
            UiSettingsValue::Integer(1), UiSettingsValue::Integer(4))) return false;
        if (!appendPathRestart("path-history-firefly-filter-reset",
            RetainedRuntimeAction::ChangeSetting, SettingId::PathingFireflyFilter,
            UiSettingsValue::Boolean(false), UiSettingsValue::Boolean(true), true)) return false;
        if (!Set(cases.Back(), FloatSetting(SettingId::PathingFireflyThreshold, 10.f), error)) return false;
        if (!Set(cases.Back(), FloatSetting(SettingId::SkyExposure, 3.f), error)) return false;
        if (!appendPathRestart("path-history-firefly-threshold-reset",
            RetainedRuntimeAction::ChangeSetting, SettingId::PathingFireflyThreshold,
            UiSettingsValue::Float(10.f), UiSettingsValue::Float(1000000.f), true)) return false;
        if (!Set(cases.Back(), FloatSetting(SettingId::SkyExposure, 3.f), error)) return false;

        const auto appendCycle = [&](RetainedRuntimeCase runtimeCase, SettingId id,
            UiSettingsValue baselineValue = UiSettingsValue::Boolean(true),
            UiSettingsValue disabledValue = UiSettingsValue::Boolean(false))
        {
            runtimeCase.action = RetainedRuntimeAction::CyclePrerequisite;
            runtimeCase.actionSettingId = id;
            runtimeCase.actionBaselineValue = std::move(baselineValue);
            runtimeCase.actionValue = std::move(disabledValue);
            if (!setScene(runtimeCase, bistroScene, BistroToken)) return false;
            if (!cases.Append(std::move(runtimeCase), error)) return false;
            return true;
        };
        RetainedRuntimeCase pathTraversal;
        if (!pathCase("path-traversal-cycle", pathTraversal)) return false;
        pathTraversal.requirePathHistoryRestart = true;
        if (!appendCycle(std::move(pathTraversal), SettingId::RepresentationAllowRayTraversal)) return false;
        RetainedRuntimeCase rasterTraversal;
        if (!Raster({"ray-marching-traversal-cycle"}, rasterTraversal, error)) return false;
        rasterTraversal.expectDirectionalVisibility = true;
        if (!appendCycle(std::move(rasterTraversal), SettingId::RepresentationAllowRayTraversal)) return false;

        std::rotate(cases.begin(), cases.end() - 2, cases.end());
        output = std::move(cases);
        return true;
    }


    RetainedRuntimeDiagnosticState::RetainedRuntimeDiagnosticState(
        RetainedRuntimeCases cases,
        Clock::time_point start) noexcept
        : m_Cases(std::move(cases))
        , m_Start(start)
        , m_CaseStart(start)
    {
    }

    std::int64_t RetainedRuntimeDiagnosticState::ElapsedMilliseconds(
        Clock::time_point now) const noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_Start).count();
    }

    std::size_t RetainedRuntimeDiagnosticState::PassedCaseCount() const noexcept
    {
        return m_PassedCases;
    }

    std::size_t RetainedRuntimeDiagnosticState::TotalCaseCount() const noexcept
    {
        return m_Cases.Count();
    }

    bool RetainedRuntimeDiagnosticState::RequiresSettingsSnapshot() const noexcept
    {
        if (m_CaseIndex >= m_Cases.Count() ||
            !m_Cases[m_CaseIndex].snapshotRoundTrip ||
            m_SnapshotCompleted)
        {
            return false;
        }
        return m_Phase == Phase::WaitForEvidence ||
            m_Phase == Phase::WaitForResetFrame ||
            m_Phase == Phase::WaitForRestoredEvidence;
    }

    RetainedRuntimeDirective RetainedRuntimeDiagnosticState::Finish(
        bool passed,
        RetainedRuntimeMessage message) noexcept
    {
        m_Phase = Phase::Complete;
        RetainedRuntimeDirective directive{
            passed
                ? RetainedRuntimeDirectiveKind::FinishPass
                : RetainedRuntimeDirectiveKind::FinishFail,
            m_CaseIndex < m_Cases.Count()
                ? &m_Cases[m_CaseIndex]
                : nullptr,
            m_CaseIndex
        };
        directive.failure = message;
        return directive;
    }

    RetainedRuntimeDirective RetainedRuntimeDiagnosticState::Abort(
        RetainedRuntimeMessage message,
        Clock::time_point) noexcept
    {
        return Finish(false, message);
    }

    bool RetainedRuntimeDiagnosticState::EvidenceReady(
        const RetainedRuntimeCase& runtimeCase,
        const RetainedRuntimeTelemetry& telemetry,
        bool beforeAction,
        RetainedRuntimeMessage& reason) noexcept
    {
        if (telemetry.sceneBusy)
        {
            reason = "scene is busy";
            return false;
        }
        if (!telemetry.sceneLoaded)
        {
            reason = "scene failed to load";
            return false;
        }
        const bool baselineScenePhase = beforeAction ||
            (runtimeCase.exerciseRetainedStateChanges &&
                m_CurrentAction == RetainedRuntimeAction::NudgeCamera);
        const std::string_view expectedScene =
            baselineScenePhase &&
                !runtimeCase.actionBaselineSceneToken.View().empty()
                ? runtimeCase.actionBaselineSceneToken.View()
                : runtimeCase.expectedSceneToken.View();
        if (!expectedScene.empty() &&
            !ContainsLoweredSceneText(telemetry.currentScene, expectedScene))
        {
            reason = {"active scene does not match '", expectedScene, "'"};
            return false;
        }

        if (runtimeCase.expectDirectionalVisibility &&
            !telemetry.directionalVisibilityDispatched)
        {
            reason = "directional per-sample visibility did not dispatch";
            return false;
        }
        if (runtimeCase.expectSkyVisibility &&
            !telemetry.skyVisibilityDispatched)
        {
            reason = "sky per-sample visibility did not dispatch";
            return false;
        }
        const bool flashlightDisabledReference = !beforeAction &&
            m_CurrentAction == RetainedRuntimeAction::ChangeSetting &&
            runtimeCase.actionSettingId ==
                SettingId::LightSelectedFlashlightEnabled &&
            IsBoolean(&runtimeCase.actionValue, false);
        const bool flashlightShadowsDisabledReference = !beforeAction &&
            m_CurrentAction == RetainedRuntimeAction::ChangeSetting &&
            (flashlightDisabledReference ||
                (runtimeCase.actionSettingId ==
                        SettingId::LightSelectedFlashlightCastShadows &&
                    IsBoolean(&runtimeCase.actionValue, false)));
        const bool expectedFlashlightLighting =
            runtimeCase.expectFlashlightLightingSubmitted &&
            !flashlightDisabledReference;
        if (runtimeCase.assertFlashlightLightingState &&
            telemetry.flashlightLightingSubmitted !=
                expectedFlashlightLighting)
        {
            reason = expectedFlashlightLighting
                ? "flashlight lighting was not submitted"
                : "disabled flashlight still contributed direct lighting";
            return false;
        }
        const bool expectedFlashlightVisibility =
            runtimeCase.expectFlashlightVisibility &&
            !flashlightShadowsDisabledReference;
        if (runtimeCase.assertFlashlightVisibilityState &&
            telemetry.flashlightVisibilityDispatched !=
                expectedFlashlightVisibility)
        {
            reason = expectedFlashlightVisibility
                ? "flashlight per-sample visibility did not dispatch"
                : "flashlight visibility dispatched while shadows were disabled";
            return false;
        }
        if (!runtimeCase.assertFlashlightVisibilityState &&
            runtimeCase.expectFlashlightVisibility &&
            !telemetry.flashlightVisibilityDispatched)
        {
            reason = "flashlight per-sample visibility did not dispatch";
            return false;
        }
        if (runtimeCase.expectLightingAccumulation &&
            !telemetry.lightingAccumulationCommitted)
        {
            reason = "fixed cumulative lighting history did not commit";
            return false;
        }
        if (runtimeCase.assertLightingAccumulationState &&
            telemetry.lightingAccumulationCommitted !=
                runtimeCase.expectLightingAccumulation)
        {
            reason = runtimeCase.expectLightingAccumulation
                ? "global sample accumulation did not commit"
                : "global sample accumulation committed while disabled";
            return false;
        }
        if (runtimeCase.assertAutoExposureState &&
            telemetry.autoExposureDispatched !=
                runtimeCase.expectAutoExposure)
        {
            reason = runtimeCase.expectAutoExposure
                ? "auto exposure did not dispatch"
                : "auto exposure dispatched while disabled";
            return false;
        }
        const auto phaseValue = [&](SettingId id) -> const UiSettingsValue*
        {
            if (!beforeAction &&
                m_CurrentAction == RetainedRuntimeAction::ChangeSetting &&
                runtimeCase.actionSettingId == id)
                return &runtimeCase.actionValue;
            return Get(runtimeCase, id);
        };
        const UiSettingsValue* expectedNoisePattern =
            phaseValue(SettingId::NoisePattern);
        const UiSettingsValue* expectedNoiseResolution =
            phaseValue(SettingId::NoiseResolution);
        const UiSettingsValue* expectedNoiseAnimate =
            phaseValue(SettingId::NoiseAnimateSamples);
        const UiSettingsValue* expectedNoiseAccumulate =
            phaseValue(SettingId::NoiseAccumulateSamples);
        if ((expectedNoisePattern &&
                telemetry.globalNoisePattern != expectedNoisePattern->Text()) ||
            (expectedNoiseResolution &&
                telemetry.globalNoiseResolution !=
                    expectedNoiseResolution->Text()) ||
            (expectedNoiseAnimate &&
                telemetry.globalNoiseAnimateSamples !=
                    expectedNoiseAnimate->boolean) ||
            (expectedNoiseAccumulate &&
                telemetry.globalNoiseAccumulateSamples !=
                    expectedNoiseAccumulate->boolean))
        {
            reason = "the applied global noise state did not match the case";
            return false;
        }
        if (!beforeAction &&
            m_CurrentAction != RetainedRuntimeAction::None &&
            telemetry.lastAppliedAction != m_CurrentAction)
        {
            reason = "the named runtime action was not applied";
            return false;
        }
        if (runtimeCase.expectedPathHistoryCount > 0u)
        {
            if (!beforeAction &&
                runtimeCase.requirePathHistoryRestart &&
                telemetry.pathHistoryGeneration != 0u &&
                telemetry.pathHistoryGeneration != m_PathGenerationBeforeAction)
            {
                m_ObservedPathRestart = true;
            }
            if (!beforeAction &&
                runtimeCase.requirePathHistoryRestart &&
                !m_ObservedPathRestart)
            {
                reason = "path history generation did not change after the action";
                return false;
            }
            if (telemetry.pathHistoryCount <
                runtimeCase.expectedPathHistoryCount)
            {
                reason = RetainedRuntimeMessage::PathCount(
                    telemetry.pathHistoryCount, runtimeCase.expectedPathHistoryCount);
                return false;
            }
        }
        reason = {};
        return true;
    }

    RetainedRuntimeDirective RetainedRuntimeDiagnosticState::Tick(
        const RetainedRuntimeTelemetry& telemetry,
        Clock::time_point now) noexcept
    {
        if (m_Phase == Phase::Complete)
            return {};
        if (now - m_Start > RetainedRuntimeGlobalTimeout)
            return Finish(false, "global six-hour timeout expired");

        if (m_Phase == Phase::Apply)
        {
            if (m_CaseIndex >= m_Cases.Count())
            {
                const auto semantic = m_SemanticSummary.Validate(m_Cases.Data(), m_Cases.Count());
                auto directive = Finish(semantic.Passed(), {});
                directive.semanticFailure = semantic;
                return directive;
            }
            m_SemanticSummary.BeginCase();
            m_CaseStart = now;
            m_SettledFrames = 0u;
            m_CompletedActionCount = 0u;
            m_CurrentAction = RetainedRuntimeAction::None;
            m_PathGenerationBeforeAction = 0u;
            m_ObservedPathRestart =
                !m_Cases[m_CaseIndex].requirePathHistoryRestart;
            m_SnapshotCompleted =
                !m_Cases[m_CaseIndex].snapshotRoundTrip;
            m_TimingRecoverySampleUsed = false;
            m_LastActiveLinearHash = 0u;
            m_BaselineCpuMilliseconds = 0.0;
            m_BaselineGpuMilliseconds = 0.0;
            m_CaptureCpuMilliseconds = 0.0;
            m_CaptureGpuMilliseconds = 0.0;
            m_SavedSnapshot = {};
            m_WaitReason = {};
            m_Phase = Phase::WaitForEvidence;
            return {
                RetainedRuntimeDirectiveKind::ApplyCase,
                &m_Cases[m_CaseIndex],
                m_CaseIndex,
                {}
            };
        }

        if (now - m_CaseStart > std::chrono::minutes(5))
        {
            auto message = m_WaitReason;
            message.caseTimeout = true;
            return Finish(false, message);
        }
        RetainedRuntimeCase& runtimeCase = m_Cases[m_CaseIndex];

        if (m_Phase == Phase::WaitForResetFrame)
        {
            if (telemetry.sceneBusy)
                return {};
            if (++m_SettledFrames < 2u)
                return {};
            if (!telemetry.settingsSnapshot)
                return Finish(false, "RESET snapshot was not observed");
            if ((*telemetry.settingsSnapshot != m_SavedSnapshot.View()) != runtimeCase.expectSnapshotResetChange)
            {
                return Finish(false, runtimeCase.expectSnapshotResetChange
                    ? "RESET left the non-default snapshot unchanged"
                    : "RESET changed the factory-default snapshot");
            }
            m_SettledFrames = 0u;
            m_Phase = Phase::WaitForRestoredEvidence;
            RetainedRuntimeDirective directive{
                RetainedRuntimeDirectiveKind::RestoreSnapshot, &runtimeCase, m_CaseIndex
            };
            directive.snapshot = m_SavedSnapshot.View();
            return directive;
        }

        if (m_Phase == Phase::WaitForEvidence ||
            m_Phase == Phase::WaitForRestoredEvidence)
        {
            const bool beforeAction =
                m_CurrentAction == RetainedRuntimeAction::None;
            if (!EvidenceReady(
                    runtimeCase,
                    telemetry,
                    beforeAction,
                    m_WaitReason))
            {
                m_SettledFrames = 0u;
                m_TimingRecoverySampleUsed = false;
                return {};
            }
            if (++m_SettledFrames < 2u)
                return {};

            if (m_Phase == Phase::WaitForEvidence &&
                !m_SnapshotCompleted)
            {
                if (!telemetry.settingsSnapshot)
                    return Finish(false, "configured snapshot was not observed");
                SettingsSnapshotError error;
                if (!m_SavedSnapshot.Assign(*telemetry.settingsSnapshot, error))
                    return Finish(false, "configured snapshot could not be retained");
                m_SettledFrames = 0u;
                m_TimingRecoverySampleUsed = false;
                m_Phase = Phase::WaitForResetFrame;
                return {
                    RetainedRuntimeDirectiveKind::ResetSettings,
                    &runtimeCase,
                    m_CaseIndex,
                    {}
                };
            }
            if (m_Phase == Phase::WaitForRestoredEvidence &&
                (!telemetry.settingsSnapshot ||
                    *telemetry.settingsSnapshot != m_SavedSnapshot.View()))
            {
                return Finish(false, telemetry.settingsSnapshot
                    ? RetainedRuntimeMessage::SnapshotMismatch(m_SavedSnapshot.View(), *telemetry.settingsSnapshot)
                    : RetainedRuntimeMessage("saved settings did not restore the exact live snapshot: live snapshot was unavailable"));
            }
            if (!std::isfinite(telemetry.cpuFrameMilliseconds) ||
                telemetry.cpuFrameMilliseconds <= 0.0)
            {
                return Finish(false,
                    "stable CPU frame timing was unavailable or non-finite");
            }
            if (!telemetry.gpuFrameTimingAvailable)
            {
                m_SettledFrames = 0u;
                m_WaitReason = "stable GPU frame timing was unavailable";
                return {};
            }
            if (!std::isfinite(telemetry.gpuFrameMilliseconds) ||
                telemetry.gpuFrameMilliseconds <= 0.0)
            {
                return Finish(false,
                    "stable GPU frame timing was unavailable or non-finite");
            }
            m_CaptureCpuMilliseconds = telemetry.cpuFrameMilliseconds;
            m_CaptureGpuMilliseconds = telemetry.gpuFrameMilliseconds;
            const auto deferOneTimingSample =
                [this](RetainedRuntimeMessage waitReason)
                {
                    if (m_TimingRecoverySampleUsed)
                        return false;
                    m_TimingRecoverySampleUsed = true;
                    m_SettledFrames = 1u;
                    m_WaitReason = waitReason;
                    return true;
                };

            if (m_CurrentAction == RetainedRuntimeAction::None)
            {
                if (m_CaptureCpuMilliseconds >
                        MaximumBaselineFrameMilliseconds ||
                    m_CaptureGpuMilliseconds >
                        MaximumBaselineFrameMilliseconds)
                {
                    if (deferOneTimingSample(
                            "waiting for one post-transition baseline timing "
                            "sample"))
                    {
                        return {};
                    }
                    return Finish(false, RetainedRuntimeMessage::BaselineTiming(
                        m_CaptureCpuMilliseconds, m_CaptureGpuMilliseconds));
                }
            }
            else
            {
                const double cpuLimit = std::max(
                    m_BaselineCpuMilliseconds *
                        FrameTimingRelativeTolerance,
                    m_BaselineCpuMilliseconds +
                        FrameTimingAdditiveToleranceMilliseconds);
                const double gpuLimit = std::max(
                    m_BaselineGpuMilliseconds *
                        FrameTimingRelativeTolerance,
                    m_BaselineGpuMilliseconds +
                        FrameTimingAdditiveToleranceMilliseconds);
                if (m_CaptureCpuMilliseconds > cpuLimit ||
                    m_CaptureGpuMilliseconds > gpuLimit)
                {
                    if (deferOneTimingSample(
                            "waiting for one post-transition action timing "
                            "sample"))
                    {
                        return {};
                    }
                    return Finish(false, RetainedRuntimeMessage::ActionTiming(
                        m_CaptureCpuMilliseconds, cpuLimit, m_CaptureGpuMilliseconds, gpuLimit));
                }
            }
            if (m_Phase == Phase::WaitForRestoredEvidence)
                m_SnapshotCompleted = true;
            m_Phase = Phase::WaitForCapture;
            RetainedRuntimeDirective directive{
                RetainedRuntimeDirectiveKind::CaptureOutput,
                &runtimeCase,
                m_CaseIndex
            };
            directive.captureLabel = RetainedRuntimeCaptureLabel(m_CurrentAction);
            directive.hasStableFrameTiming = true;
            directive.stableCpuFrameMilliseconds =
                m_CaptureCpuMilliseconds;
            directive.stableGpuFrameMilliseconds =
                m_CaptureGpuMilliseconds;
            return directive;
        }

        if (m_Phase == Phase::WaitForCapture)
        {
            if (!telemetry.output)
                return {};
            const RuntimeOutputEvidence& output = *telemetry.output;
            const std::string_view captureLabel = RetainedRuntimeCaptureLabel(m_CurrentAction);
            if (!output.valid || output.pixelBytes == 0u ||
                output.minimumByte == output.maximumByte ||
                !output.linearReadbackValid ||
                output.nonFiniteComponentCount != 0u)
            {
                return Finish(false, RetainedRuntimeMessage::InvalidOutput(output));
            }
            if (m_CurrentAction ==
                    RetainedRuntimeAction::ResizeViewport &&
                (output.width != static_cast<std::uint32_t>(
                        runtimeCase.resizeWidth) ||
                    output.height != static_cast<std::uint32_t>(
                        runtimeCase.resizeHeight)))
            {
                return Finish(false,
                    "resize output dimensions did not match the requested viewport");
            }

            if (m_CurrentAction == RetainedRuntimeAction::None)
            {
                m_LastActiveLinearHash = output.linearHash;
                m_BaselineCpuMilliseconds =
                    m_CaptureCpuMilliseconds;
                m_BaselineGpuMilliseconds =
                    m_CaptureGpuMilliseconds;
            }
            else
            {
                const bool mustDifferFromPrior =
                    runtimeCase.requireActionOutputDifference ||
                    (runtimeCase.exerciseRetainedStateChanges &&
                        (m_CurrentAction ==
                            RetainedRuntimeAction::NudgeCamera ||
                        m_CurrentAction ==
                            RetainedRuntimeAction::ChangeScene));
                if (mustDifferFromPrior &&
                    output.linearHash == m_LastActiveLinearHash)
                {
                    return Finish(false,
                        "named camera/scene action did not change rendered output");
                }
                m_LastActiveLinearHash = output.linearHash;
            }

            if (runtimeCase.exerciseRetainedStateChanges &&
                (m_CurrentAction == RetainedRuntimeAction::None ||
                    m_CurrentAction == RetainedRuntimeAction::ChangeScene))
            {
                const std::string_view sceneToken =
                    m_CurrentAction == RetainedRuntimeAction::ChangeScene
                        ? std::string_view(runtimeCase.expectedSceneToken.View())
                        : std::string_view(runtimeCase.actionBaselineSceneToken.View());
                m_SemanticSummary.Record(runtimeCase, sceneToken, BuildRuntimeSemanticSignature(output));
            }

            if (m_CurrentAction != RetainedRuntimeAction::None)
                ++m_CompletedActionCount;
            m_CurrentAction = RetainedRuntimeAction::None;

            if (m_CompletedActionCount < ActionCount(runtimeCase))
            {
                const RetainedRuntimeAction nextAction = ActionAt(
                    runtimeCase, m_CompletedActionCount);
                if (nextAction == RetainedRuntimeAction::None)
                    return Finish(false, "runtime action sequence was incomplete");
                m_CurrentAction = nextAction;
                // sample readback can lag the capture's own reset. only an actual
                // subsequent clear proves that the named action restarted history.
                m_PathGenerationBeforeAction = telemetry.pathHistoryGeneration;
                m_ObservedPathRestart =
                    !runtimeCase.requirePathHistoryRestart;
                m_SettledFrames = 0u;
                m_TimingRecoverySampleUsed = false;
                m_Phase = Phase::WaitForEvidence;

                RetainedRuntimeDirective directive{
                    RetainedRuntimeDirectiveKind::ApplyAction,
                    &runtimeCase,
                    m_CaseIndex
                };
                directive.captureLabel = captureLabel;
                directive.action = nextAction;
                directive.resizeWidth = runtimeCase.resizeWidth;
                directive.resizeHeight = runtimeCase.resizeHeight;
                if (runtimeCase.exerciseRetainedStateChanges)
                {
                    if (nextAction == RetainedRuntimeAction::ChangeScene)
                    {
                        directive.actionSettingId = SettingId::SceneCurrent;
                    }
                }
                else
                {
                    directive.actionSettingId = runtimeCase.actionSettingId;
                }
                directive.hasStableFrameTiming = true;
                directive.stableCpuFrameMilliseconds =
                    m_CaptureCpuMilliseconds;
                directive.stableGpuFrameMilliseconds =
                    m_CaptureGpuMilliseconds;
                return directive;
            }

            m_SemanticSummary.CompleteCase(runtimeCase);
            const RetainedRuntimeCase* completed = &runtimeCase;
            const std::size_t completedIndex = m_CaseIndex;
            ++m_PassedCases;
            ++m_CaseIndex;
            m_Phase = Phase::Apply;
            RetainedRuntimeDirective directive{
                RetainedRuntimeDirectiveKind::ReportCasePass,
                completed,
                completedIndex,
                {}
            };
            directive.hasStableFrameTiming = true;
            directive.stableCpuFrameMilliseconds =
                m_CaptureCpuMilliseconds;
            directive.stableGpuFrameMilliseconds =
                m_CaptureGpuMilliseconds;
            return directive;
        }
        return {};
    }
}
