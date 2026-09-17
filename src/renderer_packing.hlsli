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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#ifndef UVSR_RENDERER_PACKING_HLSLI
#define UVSR_RENDERER_PACKING_HLSLI

float Unpack_R8_SNORM(uint value)
{
    int signedValue = int(value << 24u) >> 24;
    return clamp(float(signedValue) / 127.0f, -1.0f, 1.0f);
}

float3 Unpack_RGB8_SNORM(uint value)
{
    return float3(
        Unpack_R8_SNORM(value),
        Unpack_R8_SNORM(value >> 8u),
        Unpack_R8_SNORM(value >> 16u));
}

float4 Unpack_RGBA8_SNORM(uint value)
{
    return float4(
        Unpack_R8_SNORM(value),
        Unpack_R8_SNORM(value >> 8u),
        Unpack_R8_SNORM(value >> 16u),
        Unpack_R8_SNORM(value >> 24u));
}

uint Pack_R8_SNORM(float value)
{
    return int(clamp(value, -1.0, 1.0) * 127.0) & 0xff;
}

uint Pack_RGBA8_SNORM(float4 rgba)
{
    uint r = Pack_R8_SNORM(rgba.r);
    uint g = Pack_R8_SNORM(rgba.g) << 8;
    uint b = Pack_R8_SNORM(rgba.b) << 16;
    uint a = Pack_R8_SNORM(rgba.a) << 24;
    return r | g | b | a;
}
#endif
