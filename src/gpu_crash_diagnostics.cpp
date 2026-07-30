#include "gpu_crash_diagnostics.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>

#include <Windows.h>
#include <d3d12.h>
#include <pix.h>
#include <donut/core/log.h>

namespace uvsr
{
    namespace
    {
        bool g_DredDiagnosticsEnabled = false;

        std::string WideName(const wchar_t* name)
        {
            if (!name || name[0] == L'\0')
                return {};

            const int byteCount = WideCharToMultiByte(
                CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
            if (byteCount <= 1)
                return {};

            std::string result(size_t(byteCount), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                name,
                -1,
                result.data(),
                byteCount,
                nullptr,
                nullptr);
            result.resize(size_t(byteCount - 1));
            return result;
        }

        std::string ObjectName(
            const char* narrowName,
            const wchar_t* wideName)
        {
            const std::string converted = WideName(wideName);
            if (!converted.empty())
                return converted;
            return narrowName ? narrowName : "";
        }

        const char* OperationName(D3D12_AUTO_BREADCRUMB_OP operation)
        {
            switch (operation)
            {
            case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SetMarker";
            case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BeginEvent";
            case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "EndEvent";
            case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:
                return "DrawInstanced";
            case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:
                return "DrawIndexedInstanced";
            case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:
                return "ExecuteIndirect";
            case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return "Dispatch";
            case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:
                return "CopyBufferRegion";
            case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:
                return "CopyTextureRegion";
            case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:
                return "CopyResource";
            case D3D12_AUTO_BREADCRUMB_OP_COPYTILES: return "CopyTiles";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE:
                return "ResolveSubresource";
            case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:
                return "ClearRenderTargetView";
            case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW:
                return "ClearUnorderedAccessView";
            case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:
                return "ClearDepthStencilView";
            case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:
                return "ResourceBarrier";
            case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE:
                return "ExecuteBundle";
            case D3D12_AUTO_BREADCRUMB_OP_PRESENT: return "Present";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA:
                return "ResolveQueryData";
            case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION:
                return "BeginSubmission";
            case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION:
                return "EndSubmission";
            case D3D12_AUTO_BREADCRUMB_OP_BARRIER: return "Barrier";
            case D3D12_AUTO_BREADCRUMB_OP_BEGIN_COMMAND_LIST:
                return "BeginCommandList";
            default: return "Other";
            }
        }

        void WriteAllocationList(
            std::ofstream& report,
            const char* label,
            const D3D12_DRED_ALLOCATION_NODE1* node)
        {
            constexpr uint32_t MaximumReportedNodes = 128u;
            uint32_t index = 0u;
            for (; node && index < MaximumReportedNodes;
                node = node->pNext, ++index)
            {
                report << label << index << "Name="
                    << ObjectName(node->ObjectNameA, node->ObjectNameW)
                    << "\n";
                report << label << index << "Type="
                    << uint32_t(node->AllocationType) << "\n";
            }
            report << label << "Count=" << index << "\n";
            report << label << "Truncated=" << (node ? 1u : 0u) << "\n";
        }
    }

    bool EnableD3d12DredDiagnostics()
    {
        ID3D12DeviceRemovedExtendedDataSettings1* settings = nullptr;
        const HRESULT result = D3D12GetDebugInterface(
            IID_PPV_ARGS(&settings));
        if (FAILED(result) || !settings)
        {
            donut::log::warning(
                "DRED diagnostics could not be enabled (HRESULT 0x%08x)",
                unsigned(result));
            return false;
        }

        settings->SetAutoBreadcrumbsEnablement(
            D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->SetPageFaultEnablement(
            D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->SetBreadcrumbContextEnablement(
            D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->Release();
        g_DredDiagnosticsEnabled = true;
        donut::log::info(
            "DRED auto-breadcrumb, marker-context, and page-fault diagnostics enabled for this process");
        return true;
    }

    void NameD3d12GraphicsQueue(nvrhi::IDevice* device)
    {
        if (!device || device->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D12)
            return;

        ID3D12CommandQueue* queue = device->getNativeQueue(
            nvrhi::ObjectTypes::D3D12_CommandQueue,
            nvrhi::CommandQueue::Graphics);
        if (queue)
            queue->SetName(L"UVSR Graphics Queue");
    }

    void NameD3d12CommandList(
        nvrhi::ICommandList* commandList,
        const wchar_t* name)
    {
        if (!commandList)
            return;

        ID3D12GraphicsCommandList* nativeCommandList =
            commandList->getNativeObject(
                nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
        if (nativeCommandList)
            nativeCommandList->SetName(name);
    }

    void SetD3d12DredMarker(
        nvrhi::ICommandList* commandList,
        const wchar_t* name)
    {
        if (!g_DredDiagnosticsEnabled || !commandList || !name)
            return;

        ID3D12GraphicsCommandList* nativeCommandList =
            commandList->getNativeObject(
                nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
        if (nativeCommandList)
            PIXSetMarker(nativeCommandList, PIX_COLOR_DEFAULT, name);
    }

    bool WriteD3d12DredReport(nvrhi::IDevice* device, const char* path)
    {
        if (!device ||
            device->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D12)
        {
            return false;
        }

        ID3D12Device* nativeDevice = device->getNativeObject(
            nvrhi::ObjectTypes::D3D12_Device);
        if (!nativeDevice)
            return false;

        const HRESULT removedReason = nativeDevice->GetDeviceRemovedReason();
        if (SUCCEEDED(removedReason))
            return false;

        const std::filesystem::path reportPath(path);
        if (reportPath.has_parent_path())
        {
            std::error_code directoryError;
            std::filesystem::create_directories(
                reportPath.parent_path(),
                directoryError);
            if (directoryError)
            {
                donut::log::warning(
                    "Could not create DRED report directory '%s' (%s)",
                    reportPath.parent_path().string().c_str(),
                    directoryError.message().c_str());
                return false;
            }
        }

        std::ofstream report(
            reportPath,
            std::ios::binary | std::ios::trunc);
        if (!report)
        {
            donut::log::warning("Could not open DRED report '%s'", path);
            return false;
        }

        const std::time_t now = std::time(nullptr);
        std::tm utcTime{};
        gmtime_s(&utcTime, &now);
        report << "timestampUtc=" << std::put_time(
            &utcTime, "%Y-%m-%dT%H:%M:%SZ") << "\n";
        report << "deviceRemovedReason=0x" << std::hex
            << uint32_t(removedReason) << std::dec << "\n";

        ID3D12DeviceRemovedExtendedData1* dred = nullptr;
        const HRESULT queryResult = nativeDevice->QueryInterface(
            IID_PPV_ARGS(&dred));
        report << "dredQueryResult=0x" << std::hex
            << uint32_t(queryResult) << std::dec << "\n";
        if (FAILED(queryResult) || !dred)
        {
            report.flush();
            return false;
        }

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
        const HRESULT breadcrumbResult =
            dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
        report << "breadcrumbResult=0x" << std::hex
            << uint32_t(breadcrumbResult) << std::dec << "\n";
        constexpr uint32_t MaximumBreadcrumbNodes = 128u;
        constexpr uint32_t MaximumHistoryOperations = 65536u;
        uint32_t breadcrumbNodeIndex = 0u;
        const D3D12_AUTO_BREADCRUMB_NODE1* breadcrumbNode =
            SUCCEEDED(breadcrumbResult)
                ? breadcrumbs.pHeadAutoBreadcrumbNode
                : nullptr;
        for (; breadcrumbNode &&
            breadcrumbNodeIndex < MaximumBreadcrumbNodes;
            breadcrumbNode = breadcrumbNode->pNext,
            ++breadcrumbNodeIndex)
        {
            const uint32_t lastValue =
                breadcrumbNode->pLastBreadcrumbValue
                    ? *breadcrumbNode->pLastBreadcrumbValue
                    : 0u;
            report << "breadcrumb" << breadcrumbNodeIndex
                << "CommandList=" << ObjectName(
                    breadcrumbNode->pCommandListDebugNameA,
                    breadcrumbNode->pCommandListDebugNameW) << "\n";
            report << "breadcrumb" << breadcrumbNodeIndex
                << "Queue=" << ObjectName(
                    breadcrumbNode->pCommandQueueDebugNameA,
                    breadcrumbNode->pCommandQueueDebugNameW) << "\n";
            report << "breadcrumb" << breadcrumbNodeIndex
                << "OperationCount=" << breadcrumbNode->BreadcrumbCount
                << "\n";
            report << "breadcrumb" << breadcrumbNodeIndex
                << "LastCompletedValue=" << lastValue << "\n";

            if (breadcrumbNode->pCommandHistory &&
                breadcrumbNode->BreadcrumbCount > 0u)
            {
                const uint32_t historySize = std::min(
                    breadcrumbNode->BreadcrumbCount,
                    MaximumHistoryOperations);
                const uint32_t retainedFirstOperation =
                    breadcrumbNode->BreadcrumbCount - historySize;
                const uint32_t requestedFirstOperation = lastValue > 8u
                    ? lastValue - 8u
                    : 0u;
                const uint32_t firstOperation = std::max(
                    retainedFirstOperation,
                    requestedFirstOperation);
                const uint64_t requestedFinalOperation =
                    uint64_t(lastValue) + 4u;
                const uint32_t finalOperation = uint32_t(std::min<uint64_t>(
                    breadcrumbNode->BreadcrumbCount,
                    requestedFinalOperation));
                report << "breadcrumb" << breadcrumbNodeIndex
                    << "RetainedFirstOperation="
                    << retainedFirstOperation << "\n";
                report << "breadcrumb" << breadcrumbNodeIndex
                    << "RequestedHistoryAvailable="
                    << (requestedFirstOperation >= retainedFirstOperation
                        ? 1u
                        : 0u) << "\n";
                for (uint32_t operationIndex = firstOperation;
                    operationIndex < finalOperation;
                    ++operationIndex)
                {
                    const D3D12_AUTO_BREADCRUMB_OP operation =
                        breadcrumbNode->pCommandHistory[
                            operationIndex % historySize];
                    report << "breadcrumb" << breadcrumbNodeIndex
                        << "Operation" << operationIndex << "="
                        << OperationName(operation) << ":"
                        << uint32_t(operation) << "\n";
                }
            }

            const uint32_t contextCount =
                breadcrumbNode->pBreadcrumbContexts
                    ? std::min(
                        breadcrumbNode->BreadcrumbContextsCount, 128u)
                    : 0u;
            for (uint32_t contextIndex = 0u;
                contextIndex < contextCount;
                ++contextIndex)
            {
                const D3D12_DRED_BREADCRUMB_CONTEXT& context =
                    breadcrumbNode->pBreadcrumbContexts[contextIndex];
                report << "breadcrumb" << breadcrumbNodeIndex
                    << "Context" << contextIndex << "Index="
                    << context.BreadcrumbIndex << "\n";
                report << "breadcrumb" << breadcrumbNodeIndex
                    << "Context" << contextIndex << "Name="
                    << WideName(context.pContextString) << "\n";
            }
        }
        report << "breadcrumbNodeCount=" << breadcrumbNodeIndex << "\n";
        report << "breadcrumbNodesTruncated="
            << (breadcrumbNode ? 1u : 0u) << "\n";

        D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault{};
        const HRESULT pageFaultResult =
            dred->GetPageFaultAllocationOutput1(&pageFault);
        report << "pageFaultResult=0x" << std::hex
            << uint32_t(pageFaultResult) << std::dec << "\n";
        if (SUCCEEDED(pageFaultResult))
        {
            report << "pageFaultVa=0x" << std::hex
                << pageFault.PageFaultVA << std::dec << "\n";
            WriteAllocationList(
                report,
                "existingAllocation",
                pageFault.pHeadExistingAllocationNode);
            WriteAllocationList(
                report,
                "recentFreedAllocation",
                pageFault.pHeadRecentFreedAllocationNode);
        }

        dred->Release();
        report.flush();
        if (!report.good())
        {
            donut::log::warning("Could not finish DRED report '%s'", path);
            return false;
        }
        donut::log::info("DRED device-removal report written: %s", path);
        return true;
    }
}
