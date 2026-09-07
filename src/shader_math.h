#ifndef UVSR_SHADER_MATH_H
#define UVSR_SHADER_MATH_H

// Match shader intrinsics in CPU acceptance code. Domain equations stay in their owners.
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

using ShaderUint = std::uint32_t;
struct ShaderUint2
{
    ShaderUint x;
    ShaderUint y;
};
struct ShaderFloat2
{
    float x;
    float y;
};
struct ShaderFloat4
{
    float x;
    float y;
    float z;
    float w;
};

struct ShaderFloat3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    constexpr ShaderFloat3() noexcept = default;
    constexpr ShaderFloat3(float xValue, float yValue, float zValue) noexcept
        : x(xValue), y(yValue), z(zValue)
    {
    }
};

constexpr ShaderFloat3 operator+(
    ShaderFloat3 left,
    ShaderFloat3 right) noexcept
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

constexpr ShaderFloat3 operator-(
    ShaderFloat3 left,
    ShaderFloat3 right) noexcept
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

constexpr ShaderFloat3 operator-(ShaderFloat3 value) noexcept
{
    return { -value.x, -value.y, -value.z };
}

constexpr ShaderFloat3 operator*(
    ShaderFloat3 value,
    float scale) noexcept
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

constexpr ShaderFloat3 operator*(
    float scale,
    ShaderFloat3 value) noexcept
{
    return value * scale;
}

inline ShaderFloat3& operator*=(
    ShaderFloat3& value,
    float scale) noexcept
{
    value = value * scale;
    return value;
}

inline ShaderFloat3& operator+=(
    ShaderFloat3& left,
    ShaderFloat3 right) noexcept
{
    left = left + right;
    return left;
}

inline ShaderFloat3 operator/(
    ShaderFloat3 value,
    float divisor) noexcept
{
    return value * (1.f / divisor);
}

inline ShaderFloat3 ShaderScale3(
    ShaderFloat3 value,
    float scale) noexcept
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

inline float ShaderDot(
    ShaderFloat3 left,
    ShaderFloat3 right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline ShaderFloat3 ShaderCross(
    ShaderFloat3 left,
    ShaderFloat3 right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x
    };
}

inline float ShaderRsqrt(float value) noexcept
{
    return 1.f / std::sqrt(value);
}

inline bool ShaderIsFinite(float value) noexcept
{
    return std::isfinite(value);
}

inline float ShaderMin(float left, float right) noexcept
{
    return std::min(left, right);
}

inline float ShaderMax(float left, float right) noexcept
{
    return std::max(left, right);
}

inline float ShaderSaturate(float value) noexcept
{
    return std::clamp(value, 0.f, 1.f);
}

inline float ShaderAtan(float value) noexcept
{
    return std::atan(value);
}

inline float ShaderCos(float value) noexcept
{
    return std::cos(value);
}

inline float ShaderAcos(float value) noexcept
{
    return std::acos(value);
}

inline float ShaderSin(float value) noexcept
{
    return std::sin(value);
}

inline float ShaderSqrt(float value) noexcept
{
    return std::sqrt(value);
}

inline ShaderFloat3 ShaderMakeFloat3(
    float x,
    float y,
    float z)
{
    return { x, y, z };
}

inline ShaderFloat3 ShaderAdd3(
    ShaderFloat3 left,
    ShaderFloat3 right)
{
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z
    };
}

inline ShaderFloat3 ShaderSubtract3(
    ShaderFloat3 left,
    ShaderFloat3 right)
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z
    };
}

inline bool ShaderIsFinite3(ShaderFloat3 value)
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

inline float ShaderAbs(float value)
{
    return std::abs(value);
}

inline float ShaderPow(float value, float exponent)
{
    return std::pow(value, exponent);
}

inline float ShaderFrac(float value)
{
    return value - std::floor(value);
}

inline float ShaderAtan2(float y, float x) noexcept
{
    return std::atan2(y, x);
}

inline ShaderUint ShaderAsUint(
    float value) noexcept
{
    ShaderUint bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline ShaderFloat2 ShaderMakeFloat2(
    float x,
    float y) noexcept
{
    return { x, y };
}

inline ShaderUint2 ShaderMakeUint2(
    ShaderUint x,
    ShaderUint y) noexcept
{
    return { x, y };
}

inline ShaderFloat3 ShaderMultiply3(
    ShaderFloat3 left,
    ShaderFloat3 right) noexcept
{
    return { left.x * right.x, left.y * right.y, left.z * right.z };
}

inline ShaderFloat3 ShaderMaxZero3(
    ShaderFloat3 value) noexcept
{
    return {
        std::max(value.x, 0.f),
        std::max(value.y, 0.f),
        std::max(value.z, 0.f)
    };
}

inline bool ShaderIsNonnegative3(
    ShaderFloat3 value) noexcept
{
    return value.x >= 0.f && value.y >= 0.f && value.z >= 0.f;
}

inline float ShaderAsFloat(ShaderUint bits) noexcept
{
    float value = 0.f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline ShaderFloat4 ShaderMakeFloat4(
    float x,
    float y,
    float z,
    float w) noexcept
{
    return { x, y, z, w };
}

inline bool ShaderIsFinite4(
    ShaderFloat4 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
}

inline ShaderFloat4 ShaderLerp4(
    ShaderFloat4 left,
    ShaderFloat4 right,
    float weight) noexcept
{
    return {
        left.x + (right.x - left.x) * weight,
        left.y + (right.y - left.y) * weight,
        left.z + (right.z - left.z) * weight,
        left.w + (right.w - left.w) * weight
    };
}

inline ShaderFloat3 ShaderLerp3(
    ShaderFloat3 left,
    ShaderFloat3 right,
    float weight) noexcept
{
    return {
        left.x + (right.x - left.x) * weight,
        left.y + (right.y - left.y) * weight,
        left.z + (right.z - left.z) * weight
    };
}

#define UVSR_SHADER_INLINE inline
#define UVSR_SHADER_INOUT(type) type&
#else
#define ShaderUint uint
#define ShaderUint2 uint2
#define ShaderFloat2 float2
#define ShaderFloat3 float3
#define ShaderFloat4 float4
#define ShaderMakeFloat2 float2
#define ShaderMakeFloat3 float3
#define ShaderMakeFloat4 float4
#define ShaderMakeUint2 uint2
#define ShaderDot dot
#define ShaderCross cross
#define ShaderRsqrt rsqrt
#define ShaderIsFinite isfinite
#define ShaderMin min
#define ShaderMax max
#define ShaderSaturate saturate
#define ShaderAtan atan
#define ShaderCos cos
#define ShaderAcos acos
#define ShaderSin sin
#define ShaderSqrt sqrt
#define ShaderAbs abs
#define ShaderPow pow
#define ShaderFrac frac
#define ShaderAtan2 atan2
#define ShaderAsUint asuint
#define ShaderAsFloat asfloat
#define ShaderLerp3 lerp
#define ShaderLerp4 lerp

float3 ShaderScale3(float3 value, float scale)
{
    return value * scale;
}

float3 ShaderAdd3(float3 left, float3 right)
{
    return left + right;
}

float3 ShaderSubtract3(float3 left, float3 right)
{
    return left - right;
}

float3 ShaderMultiply3(float3 left, float3 right)
{
    return left * right;
}

float3 ShaderMaxZero3(float3 value)
{
    return max(value, 0.0f);
}

bool ShaderIsFinite3(float3 value)
{
    return all(isfinite(value));
}

bool ShaderIsFinite4(float4 value)
{
    return all(isfinite(value));
}

bool ShaderIsNonnegative3(float3 value)
{
    return all(value >= 0.0f);
}

#define UVSR_SHADER_INLINE
#define UVSR_SHADER_INOUT(type) inout type
#endif

#endif
