#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>

namespace uvsr
{
    inline constexpr std::uint32_t RendererEnvironmentShCoefficientCount =
        9u;
    inline constexpr float RendererEnvironmentHalfMaximum = 65504.f;

    struct RendererDiffuseEnvironmentSh
    {
        std::array<DirectX::XMFLOAT3,
            RendererEnvironmentShCoefficientCount> coefficients{};
    };

    struct RendererDiffuseEnvironmentProjection
    {
        RendererDiffuseEnvironmentSh sh;
        float averageLuminance = 0.f;
    };

    [[nodiscard]] inline DirectX::XMFLOAT3 RendererStoreFloat3(
        DirectX::FXMVECTOR value) noexcept
    {
        DirectX::XMFLOAT3 stored;
        DirectX::XMStoreFloat3(&stored, value);
        return stored;
    }

    [[nodiscard]] inline DirectX::XMFLOAT3 RendererSanitizeFloat3(
        DirectX::XMFLOAT3 value,
        DirectX::XMFLOAT3 fallback = {}) noexcept
    {
        return {
            std::isfinite(value.x) ? value.x : fallback.x,
            std::isfinite(value.y) ? value.y : fallback.y,
            std::isfinite(value.z) ? value.z : fallback.z
        };
    }

    [[nodiscard]] inline DirectX::XMFLOAT3 RendererClampFloat3(
        DirectX::XMFLOAT3 value,
        float minimum,
        float maximum) noexcept
    {
        using namespace DirectX;
        const XMFLOAT3 sanitizedValue = RendererSanitizeFloat3(value);
        const XMVECTOR sanitized = XMLoadFloat3(&sanitizedValue);
        return RendererStoreFloat3(XMVectorMin(
            XMVectorMax(sanitized, XMVectorReplicate(minimum)),
            XMVectorReplicate(maximum)));
    }

    [[nodiscard]] inline DirectX::XMFLOAT3
        RendererNormalizeEnvironmentDirection(
            DirectX::XMFLOAT3 value,
            DirectX::XMFLOAT3 fallback) noexcept
    {
        using namespace DirectX;
        value = RendererSanitizeFloat3(value, fallback);
        const XMVECTOR vector = XMLoadFloat3(&value);
        const float lengthSquared = XMVectorGetX(XMVector3LengthSq(vector));
        if (!(lengthSquared > 1e-12f) || !std::isfinite(lengthSquared))
            return fallback;
        return RendererStoreFloat3(XMVector3Normalize(vector));
    }

    [[nodiscard]] inline std::array<float,
        RendererEnvironmentShCoefficientCount>
        EvaluateRendererEnvironmentShBasis(
            DirectX::XMFLOAT3 direction) noexcept
    {
        direction = RendererNormalizeEnvironmentDirection(
            direction, { 0.f, 1.f, 0.f });
        constexpr float y00 = 0.28209479177387814f;
        constexpr float y1 = 0.4886025119029199f;
        constexpr float y2a = 1.0925484305920792f;
        constexpr float y20 = 0.31539156525252005f;
        constexpr float y22 = 0.5462742152960396f;
        return {
            y00,
            y1 * direction.y,
            y1 * direction.z,
            y1 * direction.x,
            y2a * direction.x * direction.y,
            y2a * direction.y * direction.z,
            y20 * (3.f * direction.z * direction.z - 1.f),
            y2a * direction.x * direction.z,
            y22 * (direction.x * direction.x -
                direction.y * direction.y)
        };
    }

    [[nodiscard]] inline std::optional<RendererDiffuseEnvironmentProjection>
        ProjectRendererDiffuseEnvironmentLatLongRgb(
            const float* pixels,
            std::uint32_t width,
            std::uint32_t height)
    {
        const std::int64_t expectedWidth = std::int64_t(height) * 2;
        if (!pixels || width < 4u || height < 2u ||
            std::abs(std::int64_t(width) - expectedWidth) > 2)
        {
            return std::nullopt;
        }

        RendererDiffuseEnvironmentProjection result;
        double accumulatedWeight = 0.0;
        constexpr std::array<std::uint32_t,
            RendererEnvironmentShCoefficientCount> bands = {
                0u, 1u, 1u, 1u, 2u, 2u, 2u, 2u, 2u
            };
        constexpr std::array<float, 3u> lambertBandScale = {
            1.f, 2.f / 3.f, 1.f / 4.f
        };

        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const double theta = (double(y) + 0.5) *
                (double(DirectX::XM_PI) / double(height));
            const float sinTheta = float(std::sin(theta));
            const float cosTheta = float(std::cos(theta));
            const double rowWeight = std::max(double(sinTheta), 0.0);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double phi = (double(x) + 0.5) *
                    (2.0 * double(DirectX::XM_PI) / double(width)) -
                    double(DirectX::XM_PI);
                const DirectX::XMFLOAT3 direction(
                    sinTheta * float(std::cos(phi)),
                    cosTheta,
                    sinTheta * float(std::sin(phi)));
                const std::size_t offset =
                    (std::size_t(y) * width + x) * 3u;
                const DirectX::XMFLOAT3 radiance = RendererClampFloat3(
                    { pixels[offset], pixels[offset + 1u],
                        pixels[offset + 2u] },
                    0.f,
                    std::numeric_limits<float>::max());
                const auto basis =
                    EvaluateRendererEnvironmentShBasis(direction);
                for (std::uint32_t coefficient = 0u;
                    coefficient < RendererEnvironmentShCoefficientCount;
                    ++coefficient)
                {
                    using namespace DirectX;
                    XMFLOAT3& destination =
                        result.sh.coefficients[coefficient];
                    XMStoreFloat3(
                        &destination,
                        XMVectorMultiplyAdd(
                            XMLoadFloat3(&radiance),
                            XMVectorReplicate(float(
                                rowWeight * double(basis[coefficient]))),
                            XMLoadFloat3(&destination)));
                }
                accumulatedWeight += rowWeight;
            }
        }

        if (!(accumulatedWeight > 0.0) ||
            !std::isfinite(accumulatedWeight))
        {
            return std::nullopt;
        }

        const float integrationScale = float(
            (4.0 * double(DirectX::XM_PI)) / accumulatedWeight);
        for (std::uint32_t coefficient = 0u;
            coefficient < RendererEnvironmentShCoefficientCount;
            ++coefficient)
        {
            DirectX::XMFLOAT3& value = result.sh.coefficients[coefficient];
            value = RendererStoreFloat3(DirectX::XMVectorScale(
                DirectX::XMLoadFloat3(&value),
                integrationScale * lambertBandScale[bands[coefficient]]));
            value = RendererSanitizeFloat3(value);
        }

        constexpr float shY00 = 0.28209479177387814f;
        const DirectX::XMFLOAT3 luminanceWeights(
            0.2126f, 0.7152f, 0.0722f);
        DirectX::XMFLOAT3 averageResponse = RendererClampFloat3(
            RendererStoreFloat3(DirectX::XMVectorScale(
                DirectX::XMLoadFloat3(&result.sh.coefficients[0]),
                shY00)),
            0.f,
            std::numeric_limits<float>::max());
        result.averageLuminance = DirectX::XMVectorGetX(
            DirectX::XMVector3Dot(
                DirectX::XMLoadFloat3(&averageResponse),
                DirectX::XMLoadFloat3(&luminanceWeights)));
        if (!(result.averageLuminance >
                std::numeric_limits<float>::epsilon()) ||
            !std::isfinite(result.averageLuminance))
        {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] inline DirectX::XMFLOAT3 EvaluateRendererEnvironmentSh(
        const RendererDiffuseEnvironmentSh& environment,
        DirectX::XMFLOAT3 direction) noexcept
    {
        using namespace DirectX;
        const auto basis = EvaluateRendererEnvironmentShBasis(direction);
        XMVECTOR result = XMVectorZero();
        for (std::uint32_t coefficient = 0u;
            coefficient < RendererEnvironmentShCoefficientCount;
            ++coefficient)
        {
            result = XMVectorMultiplyAdd(
                XMLoadFloat3(&environment.coefficients[coefficient]),
                XMVectorReplicate(basis[coefficient]),
                result);
        }
        return RendererClampFloat3(
            RendererStoreFloat3(result),
            0.f,
            std::numeric_limits<float>::max());
    }

    [[nodiscard]] inline DirectX::XMFLOAT3
        RendererEnvironmentCubeDirection(
            std::uint32_t face,
            std::uint32_t x,
            std::uint32_t y,
            std::uint32_t dimension) noexcept
    {
        dimension = std::max(dimension, 1u);
        const float u = ((float(x) + 0.5f) / float(dimension)) * 2.f - 1.f;
        const float v = 1.f -
            ((float(y) + 0.5f) / float(dimension)) * 2.f;
        DirectX::XMFLOAT3 direction;
        switch (face)
        {
        case 0u: direction = { 1.f, v, -u }; break;
        case 1u: direction = { -1.f, v, u }; break;
        case 2u: direction = { u, 1.f, -v }; break;
        case 3u: direction = { u, -1.f, v }; break;
        case 4u: direction = { u, v, 1.f }; break;
        default: direction = { -u, v, -1.f }; break;
        }
        return RendererNormalizeEnvironmentDirection(
            direction, { 0.f, 1.f, 0.f });
    }
}
