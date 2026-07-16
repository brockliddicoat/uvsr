/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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

#pragma once

#include "procedural_sky_settings.h"

#include <nvrhi/nvrhi.h>

#include <memory>

namespace donut::engine
{
    class CommonRenderPasses;
    class DirectionalLight;
    class FramebufferFactory;
    class ICompositeView;
    class ShaderFactory;
}

struct ProceduralSkyShaderParameters;

namespace uvsr
{
    class ProceduralSkyPass
    {
    public:
        ProceduralSkyPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>& commonPasses,
            const std::shared_ptr<donut::engine::FramebufferFactory>& framebufferFactory,
            const donut::engine::ICompositeView& compositeView);

        void Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::ICompositeView& compositeView,
            const donut::engine::DirectionalLight& light,
            const ProceduralSkySettings& settings) const;

        static void FillShaderParameters(
            const donut::engine::DirectionalLight& light,
            const ProceduralSkySettings& settings,
            ProceduralSkyShaderParameters& output);

    private:
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::BufferHandle m_SkyCB;
        nvrhi::BindingLayoutHandle m_RenderBindingLayout;
        nvrhi::BindingSetHandle m_RenderBindingSet;
        nvrhi::GraphicsPipelineHandle m_RenderPso;
        std::shared_ptr<donut::engine::FramebufferFactory> m_FramebufferFactory;
    };
}
