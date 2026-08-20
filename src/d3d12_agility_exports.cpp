#ifndef UVSR_D3D12_AGILITY_SDK_VERSION
#error UVSR_D3D12_AGILITY_SDK_VERSION must match the packaged D3D12Core.dll.
#endif

extern "C"
{
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion =
        UVSR_D3D12_AGILITY_SDK_VERSION;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}
