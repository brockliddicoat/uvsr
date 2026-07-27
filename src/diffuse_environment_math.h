#pragma once

#include <donut/core/math/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace uvsr
{
    inline constexpr uint32_t DiffuseEnvironmentShCoefficientCount = 9u;
    inline constexpr float DiffuseEnvironmentHalfMaximum = 65504.f;

    struct DiffuseEnvironmentSh
    {
        std::array<dm::float3, DiffuseEnvironmentShCoefficientCount>
            coefficients;

        DiffuseEnvironmentSh()
        {
            coefficients.fill(dm::float3(0.f));
        }
    };

    struct ImportedDiffuseEnvironmentProjection
    {
        DiffuseEnvironmentSh sh;
        float averageLuminance = 0.f;
    };

    inline float SanitizeFinite(float value, float fallback = 0.f)
    {
        return std::isfinite(value) ? value : fallback;
    }

    inline dm::float3 SanitizeFinite(
        dm::float3 value,
        dm::float3 fallback = 0.f)
    {
        return dm::float3(
            SanitizeFinite(value.x, fallback.x),
            SanitizeFinite(value.y, fallback.y),
            SanitizeFinite(value.z, fallback.z));
    }

    inline dm::float3 NormalizeDiffuseEnvironmentDirection(
        dm::float3 value,
        dm::float3 fallback)
    {
        value = SanitizeFinite(value, fallback);
        const float lengthSquared = dot(value, value);
        if (!(lengthSquared > 1e-12f) ||
            !std::isfinite(lengthSquared))
        {
            return fallback;
        }
        return value / std::sqrt(lengthSquared);
    }

    inline dm::float3 ClampDiffuseEnvironmentForHalf(dm::float3 value)
    {
        return min(
            max(SanitizeFinite(value), dm::float3(0.f)),
            dm::float3(DiffuseEnvironmentHalfMaximum));
    }

    inline std::array<float, DiffuseEnvironmentShCoefficientCount>
        EvaluateDiffuseEnvironmentShBasis(dm::float3 direction)
    {
        direction = NormalizeDiffuseEnvironmentDirection(
            direction, dm::float3(0.f, 1.f, 0.f));
        constexpr float Y00 = 0.28209479177387814f;
        constexpr float Y1 = 0.4886025119029199f;
        constexpr float Y2A = 1.0925484305920792f;
        constexpr float Y20 = 0.31539156525252005f;
        constexpr float Y22 = 0.5462742152960396f;
        return {
            Y00,
            Y1 * direction.y,
            Y1 * direction.z,
            Y1 * direction.x,
            Y2A * direction.x * direction.y,
            Y2A * direction.y * direction.z,
            Y20 * (3.f * direction.z * direction.z - 1.f),
            Y2A * direction.x * direction.z,
            Y22 * (direction.x * direction.x -
                direction.y * direction.y)
        };
    }

    // Project a scene-linear 2:1 latitude-longitude source into SH9 after
    // applying the Lambert kernel divided by pi. The resulting coefficients
    // therefore evaluate to unit-albedo outgoing diffuse response E / pi; the
    // receiver applies material diffuse weight but no second pi factor.
    inline std::optional<ImportedDiffuseEnvironmentProjection>
        ProjectDiffuseEnvironmentLatLongRgb(
            const float* pixels,
            uint32_t width,
            uint32_t height)
    {
        const int64_t expectedWidth = int64_t(height) * 2;
        if (!pixels ||
            width < 4u ||
            height < 2u ||
            std::abs(int64_t(width) - expectedWidth) > 2)
        {
            return std::nullopt;
        }

        ImportedDiffuseEnvironmentProjection result;
        double accumulatedWeight = 0.0;
        constexpr std::array<uint32_t,
            DiffuseEnvironmentShCoefficientCount> bands = {
                0u, 1u, 1u, 1u, 2u, 2u, 2u, 2u, 2u
            };
        constexpr std::array<float, 3> lambertBandScale = {
            1.f, 2.f / 3.f, 1.f / 4.f
        };

        for (uint32_t y = 0u; y < height; ++y)
        {
            const double theta =
                (double(y) + 0.5) *
                (double(dm::PI_f) / double(height));
            const float sinTheta = float(std::sin(theta));
            const float cosTheta = float(std::cos(theta));
            const double rowWeight = std::max(
                double(sinTheta), 0.0);
            for (uint32_t x = 0u; x < width; ++x)
            {
                const double phi =
                    (double(x) + 0.5) *
                    (2.0 * double(dm::PI_f) / double(width)) -
                    double(dm::PI_f);
                const dm::float3 direction(
                    sinTheta * float(std::cos(phi)),
                    cosTheta,
                    sinTheta * float(std::sin(phi)));
                const size_t pixelOffset =
                    (size_t(y) * size_t(width) + size_t(x)) * 3u;
                const dm::float3 radiance = max(
                    SanitizeFinite(dm::float3(
                        pixels[pixelOffset + 0u],
                        pixels[pixelOffset + 1u],
                        pixels[pixelOffset + 2u])),
                    dm::float3(0.f));
                const auto basis =
                    EvaluateDiffuseEnvironmentShBasis(direction);
                for (uint32_t coefficient = 0u;
                    coefficient <
                        DiffuseEnvironmentShCoefficientCount;
                    ++coefficient)
                {
                    result.sh.coefficients[coefficient] +=
                        radiance *
                        float(rowWeight *
                            double(basis[coefficient]));
                }
                accumulatedWeight += rowWeight;
            }
        }

        if (!(accumulatedWeight > 0.0) ||
            !std::isfinite(accumulatedWeight))
        {
            return std::nullopt;
        }

        const float integrationScale =
            float((4.0 * double(dm::PI_f)) / accumulatedWeight);
        for (uint32_t coefficient = 0u;
            coefficient < DiffuseEnvironmentShCoefficientCount;
            ++coefficient)
        {
            result.sh.coefficients[coefficient] =
                SanitizeFinite(
                    result.sh.coefficients[coefficient] *
                    (integrationScale *
                        lambertBandScale[bands[coefficient]]));
        }

        constexpr float ShY00 = 0.28209479177387814f;
        constexpr dm::float3 luminanceWeights(
            0.2126f, 0.7152f, 0.0722f);
        const dm::float3 averageResponse =
            result.sh.coefficients[0] * ShY00;
        result.averageLuminance = dot(
            max(averageResponse, dm::float3(0.f)),
            luminanceWeights);
        if (!(result.averageLuminance >
                std::numeric_limits<float>::epsilon()) ||
            !std::isfinite(result.averageLuminance))
        {
            return std::nullopt;
        }

        return result;
    }

    inline dm::float3 EvaluateDiffuseEnvironmentSh(
        const DiffuseEnvironmentSh& environment,
        dm::float3 direction)
    {
        const auto basis =
            EvaluateDiffuseEnvironmentShBasis(direction);
        dm::float3 result = 0.f;
        for (uint32_t coefficient = 0u;
            coefficient < DiffuseEnvironmentShCoefficientCount;
            ++coefficient)
        {
            result += environment.coefficients[coefficient] *
                basis[coefficient];
        }
        return max(SanitizeFinite(result), dm::float3(0.f));
    }

    inline dm::float3 DiffuseEnvironmentCubeDirection(
        uint32_t face,
        uint32_t x,
        uint32_t y,
        uint32_t dimension)
    {
        dimension = std::max(dimension, 1u);
        const float u =
            ((float(x) + 0.5f) / float(dimension)) * 2.f - 1.f;
        const float v =
            1.f - ((float(y) + 0.5f) / float(dimension)) * 2.f;
        dm::float3 direction;
        switch (face)
        {
        case 0u: direction = dm::float3(1.f, v, -u); break;
        case 1u: direction = dm::float3(-1.f, v, u); break;
        case 2u: direction = dm::float3(u, 1.f, -v); break;
        case 3u: direction = dm::float3(u, -1.f, v); break;
        case 4u: direction = dm::float3(u, v, 1.f); break;
        default: direction = dm::float3(-u, v, -1.f); break;
        }
        return NormalizeDiffuseEnvironmentDirection(
            direction, dm::float3(0.f, 1.f, 0.f));
    }
}
