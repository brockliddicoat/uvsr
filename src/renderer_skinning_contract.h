/*
* Copyright (c) 2021-2024, NVIDIA CORPORATION. All rights reserved.
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

#ifndef UVSR_RENDERER_SKINNING_CONTRACT_H
#define UVSR_RENDERER_SKINNING_CONTRACT_H
#include "renderer_gpu_scalar.h"

static const UVSR_GPU_UINT RendererSkinFirstFrame = 0x01;
static const UVSR_GPU_UINT RendererSkinNormals = 0x02;
static const UVSR_GPU_UINT RendererSkinTangents = 0x04;
static const UVSR_GPU_UINT RendererSkinUV0 = 0x08;
static const UVSR_GPU_UINT RendererSkinUV1 = 0x10;

struct RendererSkinningConstants
{
    UVSR_GPU_UINT vertexCount;
    UVSR_GPU_UINT flags;
    UVSR_GPU_UINT inputPosition;
    UVSR_GPU_UINT inputNormal;
    UVSR_GPU_UINT inputTangent;
    UVSR_GPU_UINT inputUV0;
    UVSR_GPU_UINT inputUV1;
    UVSR_GPU_UINT inputJoints;
    UVSR_GPU_UINT inputWeights;
    UVSR_GPU_UINT outputPosition;
    UVSR_GPU_UINT outputPrevious;
    UVSR_GPU_UINT outputNormal;
    UVSR_GPU_UINT outputTangent;
    UVSR_GPU_UINT outputUV0;
    UVSR_GPU_UINT outputUV1;
};

#ifdef __cplusplus
static_assert(sizeof(RendererSkinningConstants) == 60);
static_assert(offsetof(RendererSkinningConstants, vertexCount) == 0);
static_assert(offsetof(RendererSkinningConstants, flags) == 4);
static_assert(offsetof(RendererSkinningConstants, inputPosition) == 8);
static_assert(offsetof(RendererSkinningConstants, inputNormal) == 12);
static_assert(offsetof(RendererSkinningConstants, inputTangent) == 16);
static_assert(offsetof(RendererSkinningConstants, inputUV0) == 20);
static_assert(offsetof(RendererSkinningConstants, inputUV1) == 24);
static_assert(offsetof(RendererSkinningConstants, inputJoints) == 28);
static_assert(offsetof(RendererSkinningConstants, inputWeights) == 32);
static_assert(offsetof(RendererSkinningConstants, outputPosition) == 36);
static_assert(offsetof(RendererSkinningConstants, outputPrevious) == 40);
static_assert(offsetof(RendererSkinningConstants, outputNormal) == 44);
static_assert(offsetof(RendererSkinningConstants, outputTangent) == 48);
static_assert(offsetof(RendererSkinningConstants, outputUV0) == 52);
static_assert(offsetof(RendererSkinningConstants, outputUV1) == 56);
#endif
#endif
