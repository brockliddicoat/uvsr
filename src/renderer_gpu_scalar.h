/*
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef UVSR_RENDERER_GPU_SCALAR_H
#define UVSR_RENDERER_GPU_SCALAR_H

#ifdef __cplusplus
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <float.h>

static_assert(CHAR_BIT == 8 && sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
    "GPU scalar storage requires 8-bit bytes and binary32 floats");
static_assert(sizeof(int) == 4, "shared HLSL int fields require 32-bit host int");

namespace uvsr::gpu_contract
{
    struct Float2
    {
        float x;
        float y;
    };

    struct Float3
    {
        float x;
        float y;
        float z;
    };

    struct Float4
    {
        float x;
        float y;
        float z;
        float w;
    };

    struct Int2
    {
        int32_t x;
        int32_t y;
    };

    struct Int4
    {
        int32_t values[4];

        constexpr int32_t& operator[](size_t index)
        {
            return values[index];
        }

        constexpr const int32_t& operator[](size_t index) const
        {
            return values[index];
        }
    };

    struct Uint2
    {
        uint32_t x;
        uint32_t y;
    };

    struct Uint3
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
    };

    struct Uint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    // row-major 3x4 packing, multiplied on the left of a local column vector.
    struct Float3x4
    {
        float values[12];
    };

    // row-major 4x4 packing, multiplied on the right of a row vector.
    struct Float4x4
    {
        float values[16];
    };
}

#define UVSR_GPU_FLOAT2 uvsr::gpu_contract::Float2
#define UVSR_GPU_FLOAT3 uvsr::gpu_contract::Float3
#define UVSR_GPU_FLOAT4 uvsr::gpu_contract::Float4
#define UVSR_GPU_FLOAT3X4 uvsr::gpu_contract::Float3x4
#define UVSR_GPU_FLOAT4X4 uvsr::gpu_contract::Float4x4
#define UVSR_GPU_INT2 uvsr::gpu_contract::Int2
#define UVSR_GPU_INT4 uvsr::gpu_contract::Int4
#define UVSR_GPU_UINT uint32_t
#define UVSR_GPU_UINT2 uvsr::gpu_contract::Uint2
#define UVSR_GPU_UINT3 uvsr::gpu_contract::Uint3
#define UVSR_GPU_UINT4 uvsr::gpu_contract::Uint4
#else
#define UVSR_GPU_FLOAT2 float2
#define UVSR_GPU_FLOAT3 float3
#define UVSR_GPU_FLOAT4 float4
#define UVSR_GPU_FLOAT3X4 float3x4
#define UVSR_GPU_FLOAT4X4 float4x4
#define UVSR_GPU_INT2 int2
#define UVSR_GPU_INT4 int4
#define UVSR_GPU_UINT uint
#define UVSR_GPU_UINT2 uint2
#define UVSR_GPU_UINT3 uint3
#define UVSR_GPU_UINT4 uint4
#endif

#endif
