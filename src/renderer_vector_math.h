#pragma once

#include "renderer_gpu_scalar.h"

namespace uvsr::gpu_contract
{
    constexpr Float3 operator+(Float3 a, Float3 b) noexcept
    { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    constexpr Float3 operator-(Float3 a, Float3 b) noexcept
    { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    constexpr Float3 operator-(Float3 a) noexcept
    { return {-a.x, -a.y, -a.z}; }
    constexpr Float3 operator*(Float3 a, float scale) noexcept
    { return {a.x * scale, a.y * scale, a.z * scale}; }
    constexpr Float3 operator*(float scale, Float3 a) noexcept
    { return a * scale; }
    constexpr Float3 operator/(Float3 a, float scale) noexcept
    { return {a.x / scale, a.y / scale, a.z / scale}; }
    constexpr Float3& operator+=(Float3& a, Float3 b) noexcept
    { a = a + b; return a; }
    constexpr Float3& operator-=(Float3& a, Float3 b) noexcept
    { a = a - b; return a; }
    constexpr Float3& operator*=(Float3& a, float scale) noexcept
    { a = a * scale; return a; }
    constexpr bool operator==(Float3 a, Float3 b) noexcept
    { return a.x == b.x && a.y == b.y && a.z == b.z; }
    constexpr bool operator!=(Float3 a, Float3 b) noexcept
    { return !(a == b); }
}

namespace uvsr
{
    inline constexpr float RendererPiF = 3.141592654f;
    inline constexpr double RendererPiD = 3.14159265358979323;

    constexpr float Dot(gpu_contract::Float3 a, gpu_contract::Float3 b) noexcept
    { return a.x * b.x + a.y * b.y + a.z * b.z; }
    constexpr gpu_contract::Float3 Cross(gpu_contract::Float3 a, gpu_contract::Float3 b) noexcept
    { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    constexpr float LengthSquared(gpu_contract::Float3 a) noexcept
    { return Dot(a, a); }
    [[nodiscard]] float Length(gpu_contract::Float3 value) noexcept;
    [[nodiscard]] gpu_contract::Float3 Normalize(gpu_contract::Float3 value) noexcept;
    constexpr float Radians(float degrees) noexcept { return degrees * (RendererPiF / 180.f); }
    constexpr double Radians(double degrees) noexcept { return degrees * (RendererPiD / 180.0); }
    constexpr float Degrees(float radians) noexcept { return radians * (180.f / RendererPiF); }
}
