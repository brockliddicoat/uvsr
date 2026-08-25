#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = 619u;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

namespace
{
    using Microsoft::WRL::ComPtr;

    struct FormatEntry
    {
        DXGI_FORMAT format;
        std::string_view name;
    };

    constexpr std::array Formats = {
        FormatEntry{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            "R8G8B8A8_UNORM_SRGB" },
        FormatEntry{ DXGI_FORMAT_R8G8B8A8_UNORM,
            "R8G8B8A8_UNORM" },
        FormatEntry{ DXGI_FORMAT_R16G16B16A16_SNORM,
            "R16G16B16A16_SNORM" },
        FormatEntry{ DXGI_FORMAT_R16G16B16A16_FLOAT,
            "R16G16B16A16_FLOAT" },
        FormatEntry{ DXGI_FORMAT_R16G16_FLOAT, "R16G16_FLOAT" },
        FormatEntry{ DXGI_FORMAT_R8_UNORM, "R8_UNORM" },
        FormatEntry{ DXGI_FORMAT_D24_UNORM_S8_UINT,
            "D24_UNORM_S8_UINT" },
        FormatEntry{ DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
            "D32_FLOAT_S8X24_UINT" },
        FormatEntry{ DXGI_FORMAT_D32_FLOAT, "D32_FLOAT" },
        FormatEntry{ DXGI_FORMAT_D16_UNORM, "D16_UNORM" }
    };

    [[noreturn]] void Fail(const char* operation, HRESULT result)
    {
        std::cerr << operation << " failed with HRESULT 0x"
            << std::hex << std::uppercase
            << static_cast<unsigned long>(result) << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main()
{
    ComPtr<IDXGIFactory6> factory;
    HRESULT result = CreateDXGIFactory2(0u, IID_PPV_ARGS(&factory));
    if (FAILED(result))
        Fail("CreateDXGIFactory2", result);

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0u; ; ++index)
    {
        ComPtr<IDXGIAdapter1> candidate;
        result = factory->EnumAdapterByGpuPreference(
            index,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&candidate));
        if (result == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(result))
            Fail("EnumAdapterByGpuPreference", result);

        DXGI_ADAPTER_DESC1 description{};
        result = candidate->GetDesc1(&description);
        if (FAILED(result))
            Fail("IDXGIAdapter1::GetDesc1", result);
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u)
            continue;

        ComPtr<ID3D12Device> trialDevice;
        if (SUCCEEDED(D3D12CreateDevice(
                candidate.Get(),
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&trialDevice))))
        {
            adapter = std::move(candidate);
            break;
        }
    }
    if (!adapter)
        Fail("Select hardware D3D12 adapter", E_FAIL);

    DXGI_ADAPTER_DESC1 adapterDescription{};
    result = adapter->GetDesc1(&adapterDescription);
    if (FAILED(result))
        Fail("IDXGIAdapter1::GetDesc1", result);

    ComPtr<ID3D12Device> device;
    result = D3D12CreateDevice(
        adapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device));
    if (FAILED(result))
        Fail("D3D12CreateDevice", result);

    std::wcout << L"adapter=" << adapterDescription.Description << L'\n';
    std::cout << "format";
    for (UINT sampleCount : { 1u, 2u, 4u, 8u, 16u })
        std::cout << ',' << sampleCount << 'x';
    std::cout << '\n';

    for (const FormatEntry& entry : Formats)
    {
        std::cout << entry.name;
        for (UINT sampleCount : { 1u, 2u, 4u, 8u, 16u })
        {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS query{};
            query.Format = entry.format;
            query.SampleCount = sampleCount;
            query.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
            result = device->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                &query,
                sizeof(query));
            if (FAILED(result))
                Fail("CheckFeatureSupport(MULTISAMPLE_QUALITY_LEVELS)", result);
            std::cout << ',' << query.NumQualityLevels;
        }
        std::cout << '\n';
    }

    return EXIT_SUCCESS;
}
