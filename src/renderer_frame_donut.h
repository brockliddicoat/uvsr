#pragma once

namespace donut::app { class DeviceManager; }
class UvsrSceneViewer;
class UIRenderer;

namespace uvsr
{
    // private stage-10 deletion seam. the binding outlives the callback, not its owners.
    struct DonutApplicationFrameBinding
    {
        donut::app::DeviceManager& host;
        UvsrSceneViewer& scene;
        UIRenderer& ui;

        DonutApplicationFrameBinding(donut::app::DeviceManager& host,
            UvsrSceneViewer& scene, UIRenderer& ui);
        ~DonutApplicationFrameBinding();
        DonutApplicationFrameBinding(const DonutApplicationFrameBinding&) = delete;
        DonutApplicationFrameBinding& operator=(const DonutApplicationFrameBinding&) = delete;
    };

    [[nodiscard]] bool RunDonutApplicationFrame(
        donut::app::DeviceManager& host, void* context);
}
