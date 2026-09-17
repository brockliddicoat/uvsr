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

/*
License for glfw

Copyright (c) 2002-2006 Marcus Geelnard

Copyright (c) 2006-2019 Camilla Lowy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
*/

// altered from pinned Donut DeviceManager.cpp; uvsr owns the concrete frame order.
#include "renderer_frame_donut.h"
#include "uvsr_ui_internal.h"
#include <donut/app/DeviceManager.h>
#include <chrono>
#include <thread>

#if DONUT_WITH_STREAMLINE
#include <StreamlineIntegration.h>
#endif

namespace uvsr
{
    DonutApplicationFrameBinding::DonutApplicationFrameBinding(
        donut::app::DeviceManager& deviceManager, UvsrSceneViewer& sceneViewer,
        UIRenderer& uiRenderer)
        : host(deviceManager), scene(sceneViewer), ui(uiRenderer)
    {
        host.m_callbacks.applicationFrameContext = this;
        host.m_callbacks.applicationFrame = RunDonutApplicationFrame;
    }

    DonutApplicationFrameBinding::~DonutApplicationFrameBinding()
    {
        host.m_callbacks.applicationFrame = nullptr;
        host.m_callbacks.applicationFrameContext = nullptr;
    }

    bool RunDonutApplicationFrame(donut::app::DeviceManager& host, void* context)
    {
        if (!context || host.HasRuntimeFailure())
            return false;
        auto& binding = *static_cast<DonutApplicationFrameBinding*>(context);
        auto& scene = binding.scene;
        auto& ui = binding.ui;
        host.m_RenderDisposition = RendererRenderDisposition::Inactive;
        const double currentTime = glfwGetTime();
        const double elapsedTime = currentTime - host.m_PreviousFrameTimestamp;
        host.UpdateJoysticks();

        const auto renderDisposition = [&host]()
        {
            if (host.m_RuntimeFailure.Failed())
                return RendererRenderDisposition::Failed;
            if (host.m_RenderDisposition == RendererRenderDisposition::Failed)
            {
                (void)host.RecordRuntimeFailure({
                    RendererShellOperation::RequiredRenderPass,
                    static_cast<uint32_t>(E_FAIL), 0u });
            }
            return host.m_RenderDisposition;
        };

        if (host.m_windowVisible && (host.m_windowIsInFocus ||
            host.ShouldRenderUnfocused() || host.m_RequestedRenderUnfocused))
        {
            if (host.m_PrevDPIScaleFactorX != host.m_DPIScaleFactorX ||
                host.m_PrevDPIScaleFactorY != host.m_DPIScaleFactorY)
            {
                host.DisplayScaleChanged();
                host.m_PrevDPIScaleFactorX = host.m_DPIScaleFactorX;
                host.m_PrevDPIScaleFactorY = host.m_DPIScaleFactorY;
            }
            host.m_RequestedRenderUnfocused = false;

            if (host.m_callbacks.beforeAnimate)
                host.m_callbacks.beforeAnimate(host, host.m_FrameIndex);
            scene.Animate(float(elapsedTime));
            scene.SetLatewarpOptions();
            ui.Animate(float(elapsedTime));
            if (ui.RequiredFontFailure()) return false;
            ui.SetLatewarpOptions();
#if DONUT_WITH_STREAMLINE
            donut::app::StreamlineIntegration::Get().SimEnd(host);
#endif
            if (host.m_callbacks.afterAnimate)
                host.m_callbacks.afterAnimate(host, host.m_FrameIndex);
            if (renderDisposition() == RendererRenderDisposition::Failed)
                return false;

            if (host.m_FrameIndex > 0 || !host.m_SkipRenderOnFirstFrame)
            {
                // animation precedes the backbuffer wait, as in the retained shell.
                if (!host.BeginFrame())
                    return false;
                uint32_t frameIndex = host.m_FrameIndex;
#if DONUT_WITH_STREAMLINE
                donut::app::StreamlineIntegration::Get().RenderStart(host);
#endif
                if (host.m_SkipRenderOnFirstFrame)
                    --frameIndex;
                if (host.m_callbacks.beforeRender)
                    host.m_callbacks.beforeRender(host, frameIndex);

                // RenderScene owns geometry, visibility, lighting, history and scene submission.
                scene.Render(host.GetCurrentFramebuffer(scene.SupportsDepthBuffer()));
                RendererRenderDisposition disposition = renderDisposition();
                if (disposition == RendererRenderDisposition::Failed)
                    return false;
                if (disposition != RendererRenderDisposition::Pending)
                {
                    ui.Render(host.GetCurrentFramebuffer(ui.SupportsDepthBuffer()));
                    disposition = renderDisposition();
                    if (disposition == RendererRenderDisposition::Failed)
                        return false;
                }

                if (disposition != RendererRenderDisposition::Pending)
                {
                    if (host.m_callbacks.afterRender)
                        host.m_callbacks.afterRender(host, frameIndex);
#if DONUT_WITH_STREAMLINE
                    donut::app::StreamlineIntegration::Get().RenderEnd(host);
                    donut::app::StreamlineIntegration::Get().PresentStart(host);
#endif
                    ui.PacePresentation();
                    const bool presented = host.Present();
#if DONUT_WITH_STREAMLINE
                    donut::app::StreamlineIntegration::Get().PresentEnd(host);
#endif
                    if (!presented)
                        return false;
                    if (host.m_callbacks.afterPresent)
                        host.m_callbacks.afterPresent(host, frameIndex);
                }
            }
        }
        else if (host.m_windowVisible)
        {
            if (host.m_callbacks.beforeAnimate)
                host.m_callbacks.beforeAnimate(host, host.m_FrameIndex);
            if (scene.ShouldAnimateUnfocused())
            {
                scene.Animate(float(elapsedTime));
                scene.SetLatewarpOptions();
            }
            if (ui.ShouldAnimateUnfocused())
            {
                ui.Animate(float(elapsedTime));
                if (ui.RequiredFontFailure()) return false;
                ui.SetLatewarpOptions();
            }
            if (host.m_callbacks.afterAnimate)
                host.m_callbacks.afterAnimate(host, host.m_FrameIndex);
            if (renderDisposition() == RendererRenderDisposition::Failed)
                return false;
        }

        // retain the old yield until the native shell replaces this private adapter.
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
        host.GetDevice()->runGarbageCollection();
        host.UpdateAverageFrameTime(elapsedTime);
        host.m_PreviousFrameTimestamp = currentTime;
        // this counts attempts, including pending frames, not successful presents.
        ++host.m_FrameIndex;
        return true;
    }
}
