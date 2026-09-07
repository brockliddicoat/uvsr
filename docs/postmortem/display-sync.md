# display synchronization regression

the shared presentation change incorrectly made `DXGI_PRESENT_ALLOW_TEARING`
depend on the VRR checkbox. with both VRR and VSync off, it called `Present(0, 0)`.
that permits whole frames at vblank and broke the original unsynchronized test
reference. a capture from the affected executable recorded 2,013 presents, all
with sync interval 0, flags 0, and `AllowsTearing=0`.

the checkbox also lacked a refresh ceiling. enabling it allowed rendering at
170 to 200 FPS on the active 120 Hz panel, outside that panel's VRR ceiling.
an earlier repair moved the bars into the final output shader and kept all
scene passes running. that did not correct either presentation error. the
earlier claim that the toggle polarity was correct
missed the changed behavior when both controls were off.

the repair restores tearing permission whenever Vertical Sync is off and supported
windowed presentation permits it. a subsequent change added a VRR-specific FPS
ceiling, but that control only paced frames and never switched physical VRR.
the user requested its removal. the setting, special ceiling, UI capability
accessors, and current usage guidance are now removed. ordinary DXGI capability
detection and synchronized presentation remain unchanged.

the first paced capture exposed a second timing error: waiting before rendering
still allowed 82 of 1,115 present intervals to fall below 1/120 second, despite
an average near 112 FPS. the wait now uses the existing `beforePresent` callback,
after rendering. the deadline advances from the actual presentation boundary,
so late frames do not cause catch-up bursts. it rechecks the clock after waiting
rather than assuming that a timer wake reached the deadline. the graph measures
those boundaries.

the subsequent repair incorrectly restored an isolated test that bypassed scene
rendering. the test could exceed 1,000 FPS, reducing the distance the bars moved
between frames and making tearing harder to see. matching the scene's frame
rate is intended behavior. the scene-render and animation bypasses are now
removed: the bars cover the rendered scene through the existing UI draw list.
the extra output shader constants and separate binding layout remain removed.
scene and test use one workload, swap chain, and presentation policy. the test
reports timing and visible motion, with no automatic pass claim.

## frame limit and Vertical Sync

a reported 240 FPS limit still produced 120 FPS because the active display mode
was 120 Hz. the software limit was applied, but `Present(1, 0)` synchronized
whole frames to vertical blanks. a frame cap does not change the display mode
or override Vertical Sync. the old UI reported only the software pacing ceiling,
which misleadingly showed 240 while the display imposed 120.

the UI now uses **Vertical Sync**, reports the effective ceiling, and explains
when display refresh imposes it. limits below the refresh rate still pace at
the presentation boundary. the display test keeps the real scene workload.
the synchronization semantics follow [IDXGISwapChain::Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present).

## frame limiter precision

a later 280 FPS test exposed excess waiting with Vertical Sync off. the limiter
asked the Windows timer to wake at the exact deadline, then scheduled the next
deadline from the actual wake. scheduler latency therefore reduced the frame
rate on every frame. the wait now sleeps until half a millisecond before the
deadline and finishes with processor pauses. it keeps the no-catch-up policy
without repeatedly adding a scheduler round trip at the deadline.

## evidence and limits

the internal BOE panel was driven by Intel Arc at approximately 120 Hz during
the original investigation. Windows advertised 120, 240, and 480 Hz modes at
1920x1200. with user approval, the active mode was changed to 240 Hz and
verified through QueryDisplayConfig.
Intel's display API reports adaptive sync supported and enabled. the NVIDIA
adapter has no attached output, so an unavailable NVIDIA VRR query does not
mean the panel lacks VRR. the refresh change did not change driver VRR settings.

policy tests cover Vertical Sync on and off, lower and higher user limits, test
targets, effective ceilings, and unavailable refresh information. actual `Present`
flags and timing must also be checked on the rebuilt executable. lighting
regression tests do not establish presentation correctness. screenshots and
tearing permission cannot prove physical scanout is tear free; that requires
observation of moving output and, where available, the display's live counter.

Microsoft recommends tearing permission for supported windowed presents with
sync interval zero in its [VRR guidance](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays).
NVIDIA describes the whole-frame behavior without that flag in its
[swap-chain guidance](https://developer.nvidia.com/blog/advanced-api-performance-swap-chains/).
Intel documents the supported and enabled display flags in its
[control API](https://intel.github.io/drivers.gpu.control-library/Control/api.html).
[PresentMon's field definitions](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md)
distinguish tearing permission from a guarantee that a partial frame is displayed.
