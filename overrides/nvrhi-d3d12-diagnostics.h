#pragma once

// First-party diagnostics shared by UVSR's staged NVRHI D3D12 overrides.
// This header is copied beside the patched sources at configure time; the
// pinned dependency checkout remains unchanged.

namespace nvrhi::d3d12::uvsr_diagnostics
{
    inline bool IsDeviceRemovalFailure(
        HRESULT operationResult,
        HRESULT removedReason)
    {
        return operationResult == DXGI_ERROR_DEVICE_REMOVED ||
            operationResult == DXGI_ERROR_DEVICE_HUNG ||
            operationResult == DXGI_ERROR_DEVICE_RESET ||
            operationResult == DXGI_ERROR_DRIVER_INTERNAL_ERROR ||
            FAILED(removedReason);
    }

    inline void AppendAllocationNodes(
        std::stringstream& stream,
        const char* label,
        const D3D12_DRED_ALLOCATION_NODE1* node)
    {
        uint32_t index = 0u;
        for (; node && index < 8u; node = node->pNext, ++index)
        {
            stream << "\nDRED " << label << '[' << index << "]: '"
                << (node->ObjectNameA ? node->ObjectNameA : "<unnamed>")
                << "', type=" << static_cast<uint32_t>(node->AllocationType);
        }
        if (node)
            stream << "\nDRED " << label << ": additional nodes omitted";
    }

    inline void AppendDredDetails(
        std::stringstream& stream,
        ID3D12Device* device)
    {
        ID3D12DeviceRemovedExtendedData1* dred = nullptr;
        const HRESULT interfaceResult = device->QueryInterface(
            IID_PPV_ARGS(&dred));
        if (FAILED(interfaceResult) || !dred)
        {
            stream << "\nDRED 1.1 interface unavailable, HRESULT = 0x"
                << std::hex << std::setw(8) << std::setfill('0')
                << static_cast<uint32_t>(interfaceResult);
            return;
        }

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
        const HRESULT breadcrumbResult =
            dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
        if (SUCCEEDED(breadcrumbResult))
        {
            const D3D12_AUTO_BREADCRUMB_NODE1* node =
                breadcrumbs.pHeadAutoBreadcrumbNode;
            uint32_t index = 0u;
            for (; node && index < 16u; node = node->pNext, ++index)
            {
                const uint32_t completed = node->pLastBreadcrumbValue
                    ? *node->pLastBreadcrumbValue
                    : 0u;
                stream << "\nDRED breadcrumb[" << std::dec << index
                    << "]: list='"
                    << (node->pCommandListDebugNameA
                        ? node->pCommandListDebugNameA : "<unnamed>")
                    << "', queue='"
                    << (node->pCommandQueueDebugNameA
                        ? node->pCommandQueueDebugNameA : "<unnamed>")
                    << "', completed=" << completed << '/'
                    << node->BreadcrumbCount;
                if (node->pCommandHistory && completed < node->BreadcrumbCount)
                {
                    stream << ", next-op="
                        << static_cast<uint32_t>(
                            node->pCommandHistory[completed]);
                }
            }
            if (node)
                stream << "\nDRED breadcrumbs: additional nodes omitted";
            if (index == 0u)
                stream << "\nDRED breadcrumbs: no data";
        }
        else
        {
            stream << "\nDRED breadcrumb query failed, HRESULT = 0x"
                << std::hex << std::setw(8) << std::setfill('0')
                << static_cast<uint32_t>(breadcrumbResult);
        }

        D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault = {};
        const HRESULT pageFaultResult =
            dred->GetPageFaultAllocationOutput1(&pageFault);
        if (SUCCEEDED(pageFaultResult))
        {
            stream << "\nDRED page-fault VA = 0x" << std::hex
                << pageFault.PageFaultVA;
            AppendAllocationNodes(
                stream,
                "existing-allocation",
                pageFault.pHeadExistingAllocationNode);
            AppendAllocationNodes(
                stream,
                "recently-freed-allocation",
                pageFault.pHeadRecentFreedAllocationNode);
        }
        else
        {
            stream << "\nDRED page-fault query failed, HRESULT = 0x"
                << std::hex << std::setw(8) << std::setfill('0')
                << static_cast<uint32_t>(pageFaultResult);
        }
        dred->Release();
    }
}
