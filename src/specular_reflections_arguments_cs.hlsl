// Compact ray dispatch adapted from AMD FidelityFX SDK 1.1.4 Hybrid
// Reflections. UVSR retains only the low-confidence ray count.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

RWByteAddressBuffer u_RayCounter : register(u0);
RWByteAddressBuffer u_IndirectArguments : register(u1);

[numthreads(1, 1, 1)]
void PrepareArguments()
{
    const uint rayCount = u_RayCounter.Load(0u);
    u_IndirectArguments.Store(0u, (rayCount + 63u) / 64u);
    u_IndirectArguments.Store(4u, 1u);
    u_IndirectArguments.Store(8u, 1u);
}
