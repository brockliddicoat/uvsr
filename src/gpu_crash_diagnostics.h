#pragma once

#include <nvrhi/nvrhi.h>

namespace uvsr
{
    // DRED is opt-in because automatic breadcrumbs have measurable overhead.
    // Enable it before creating any D3D12 device, then write a report only if
    // that device is removed.
    [[nodiscard]] bool EnableD3d12DredDiagnostics();

    void NameD3d12GraphicsQueue(nvrhi::IDevice* device);

    void NameD3d12CommandList(
        nvrhi::ICommandList* commandList,
        const wchar_t* name);

    // Records a PIX marker only while the opt-in DRED path is active. These
    // marker strings become breadcrumb contexts without affecting accepted
    // performance runs.
    void SetD3d12DredMarker(
        nvrhi::ICommandList* commandList,
        const wchar_t* name);

    [[nodiscard]] bool WriteD3d12DredReport(
        nvrhi::IDevice* device,
        const char* path);
}
