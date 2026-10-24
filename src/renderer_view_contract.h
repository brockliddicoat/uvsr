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

#ifndef UVSR_RENDERER_VIEW_CONTRACT_H
#define UVSR_RENDERER_VIEW_CONTRACT_H

#include "renderer_gpu_scalar.h"

// row-vector world -> view -> clip, positive view Z, top-left pixel coordinates.
// offset matrices include pixel jitter; NoOffset matrices exclude it.
// current perspective is infinite reverse-Z: hardware depth = near / view Z.
// no previous-view or motion field is present; history validity is owned separately.
struct RendererViewConstants
{
    UVSR_GPU_FLOAT4X4 matWorldToView;
    UVSR_GPU_FLOAT4X4 matViewToClip;
    UVSR_GPU_FLOAT4X4 matWorldToClip;
    UVSR_GPU_FLOAT4X4 matClipToView;
    UVSR_GPU_FLOAT4X4 matViewToWorld;
    UVSR_GPU_FLOAT4X4 matClipToWorld;

    UVSR_GPU_FLOAT4X4 matViewToClipNoOffset;
    UVSR_GPU_FLOAT4X4 matWorldToClipNoOffset;
    UVSR_GPU_FLOAT4X4 matClipToViewNoOffset;
    UVSR_GPU_FLOAT4X4 matClipToWorldNoOffset;

    UVSR_GPU_FLOAT2 viewportOrigin;
    UVSR_GPU_FLOAT2 viewportSize;
    UVSR_GPU_FLOAT2 viewportSizeInv;
    UVSR_GPU_FLOAT2 pixelOffset;
    UVSR_GPU_FLOAT2 clipToWindowScale;
    UVSR_GPU_FLOAT2 clipToWindowBias;
    UVSR_GPU_FLOAT2 windowToClipScale;
    UVSR_GPU_FLOAT2 windowToClipBias;
    UVSR_GPU_FLOAT4 cameraDirectionOrPosition;
};

#ifdef __cplusplus
static_assert(sizeof(RendererViewConstants) == 720 && alignof(RendererViewConstants) == 4);
static_assert(offsetof(RendererViewConstants, matViewToClipNoOffset) == 384);
static_assert(offsetof(RendererViewConstants, viewportOrigin) == 640);
static_assert(offsetof(RendererViewConstants, pixelOffset) == 664);
static_assert(offsetof(RendererViewConstants, cameraDirectionOrPosition) == 704);
#endif

#endif
