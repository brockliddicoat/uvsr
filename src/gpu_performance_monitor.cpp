#include "gpu_performance_monitor.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace uvsr
{
    namespace
    {
        constexpr auto QueryInterval = std::chrono::milliseconds(500);

        std::string NormalizeName(std::string name)
        {
            std::transform(name.begin(), name.end(), name.begin(),
                [](unsigned char character)
                {
                    return char(std::tolower(character));
                });
            return name;
        }

        bool NamesMatch(const std::string& rendererName, const char* deviceName)
        {
            const std::string normalizedDeviceName = NormalizeName(deviceName ? deviceName : "");
            return !normalizedDeviceName.empty() &&
                (normalizedDeviceName == rendererName ||
                rendererName.find(normalizedDeviceName) != std::string::npos ||
                normalizedDeviceName.find(rendererName) != std::string::npos);
        }

        uint16_t ReadUInt16(const uint8_t* bytes)
        {
            return uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8u);
        }

        uint32_t ReadUInt32(const uint8_t* bytes)
        {
            return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8u) |
                (uint32_t(bytes[2]) << 16u) | (uint32_t(bytes[3]) << 24u);
        }

        double QuerySharedMemoryBandwidthGBps()
        {
            // Integrated GPUs share the system memory fabric. SMBIOS type 17
            // reports each populated device's configured transfer rate and
            // data width, which together define the current theoretical peak.
            constexpr DWORD RawSmbiosProvider = 0x52534d42u; // 'RSMB'
            const UINT bufferSize = GetSystemFirmwareTable(
                RawSmbiosProvider, 0, nullptr, 0);
            if (bufferSize < 8)
                return 0.0;

            std::vector<uint8_t> buffer(bufferSize);
            if (GetSystemFirmwareTable(
                    RawSmbiosProvider, 0, buffer.data(), bufferSize) != bufferSize)
            {
                return 0.0;
            }

            const size_t tableEnd = std::min(
                buffer.size(), size_t(8) + size_t(ReadUInt32(buffer.data() + 4)));
            uint64_t aggregateDataWidthBits = 0;
            uint32_t configuredTransferRateMTps = 0;

            for (size_t offset = 8; offset + 4 <= tableEnd;)
            {
                const uint8_t* structure = buffer.data() + offset;
                const uint8_t type = structure[0];
                const size_t formattedLength = structure[1];
                if (formattedLength < 4 || offset + formattedLength > tableEnd)
                    break;

                if (type == 17 && formattedLength >= 34)
                {
                    const uint16_t size = ReadUInt16(structure + 12);
                    const uint16_t dataWidth = ReadUInt16(structure + 10);
                    uint32_t transferRate = ReadUInt16(structure + 32);
                    if (transferRate == 0xffffu && formattedLength >= 92)
                        transferRate = ReadUInt32(structure + 88);
                    if ((transferRate == 0 || transferRate == 0xffffu) &&
                        formattedLength >= 23)
                    {
                        transferRate = ReadUInt16(structure + 21);
                    }

                    if (size != 0 && size != 0xffffu && dataWidth != 0 &&
                        dataWidth != 0xffffu && transferRate != 0 &&
                        transferRate != 0xffffu)
                    {
                        aggregateDataWidthBits += dataWidth;
                        configuredTransferRateMTps = configuredTransferRateMTps == 0
                            ? transferRate
                            : std::min(configuredTransferRateMTps, transferRate);
                    }
                }

                size_t next = offset + formattedLength;
                while (next + 1 < tableEnd &&
                    (buffer[next] != 0 || buffer[next + 1] != 0))
                {
                    ++next;
                }
                if (next + 1 >= tableEnd)
                    break;
                offset = next + 2;
                if (type == 127)
                    break;
            }

            return double(configuredTransferRateMTps) *
                double(aggregateDataWidthBits) / 8000.0;
        }

        class NvidiaPerformanceMonitor
        {
        public:
            explicit NvidiaPerformanceMonitor(const std::string& rendererName)
            {
                if (rendererName.find("nvidia") == std::string::npos)
                    return;

                m_Module = LoadLibraryExW(
                    L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (!m_Module)
                    return;

                m_Init = LoadFunction<InitFunction>("nvmlInit_v2");
                if (!m_Init)
                    m_Init = LoadFunction<InitFunction>("nvmlInit");
                m_Shutdown = LoadFunction<ShutdownFunction>("nvmlShutdown");
                m_GetDeviceCount = LoadFunction<GetDeviceCountFunction>("nvmlDeviceGetCount_v2");
                if (!m_GetDeviceCount)
                    m_GetDeviceCount = LoadFunction<GetDeviceCountFunction>("nvmlDeviceGetCount");
                m_GetDeviceByIndex = LoadFunction<GetDeviceByIndexFunction>(
                    "nvmlDeviceGetHandleByIndex_v2");
                if (!m_GetDeviceByIndex)
                    m_GetDeviceByIndex = LoadFunction<GetDeviceByIndexFunction>(
                        "nvmlDeviceGetHandleByIndex");
                m_GetDeviceName = LoadFunction<GetDeviceNameFunction>("nvmlDeviceGetName");
                m_GetClockInfo = LoadFunction<GetClockInfoFunction>("nvmlDeviceGetClockInfo");
                m_GetMemoryBusWidth = LoadFunction<GetUnsignedMetricFunction>(
                    "nvmlDeviceGetMemoryBusWidth");
                m_GetGpuCoreCount = LoadFunction<GetUnsignedMetricFunction>(
                    "nvmlDeviceGetNumGpuCores");
                m_GetUtilizationRates =
                    LoadFunction<GetUtilizationRatesFunction>(
                        "nvmlDeviceGetUtilizationRates");

                if (!m_Init || !m_Shutdown || !m_GetDeviceCount ||
                    !m_GetDeviceByIndex || !m_GetDeviceName || !m_GetClockInfo ||
                    !m_GetMemoryBusWidth || !m_GetGpuCoreCount ||
                    m_Init() != NvmlSuccess)
                {
                    return;
                }
                m_Initialized = true;

                unsigned int deviceCount = 0;
                if (m_GetDeviceCount(&deviceCount) != NvmlSuccess)
                    return;

                for (unsigned int deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
                {
                    NvmlDevice device = nullptr;
                    char deviceName[256]{};
                    if (m_GetDeviceByIndex(deviceIndex, &device) != NvmlSuccess ||
                        m_GetDeviceName(
                            device, deviceName, unsigned(std::size(deviceName))) !=
                            NvmlSuccess)
                    {
                        continue;
                    }

                    if (NamesMatch(rendererName, deviceName))
                    {
                        m_Device = device;
                        break;
                    }
                }
            }

            ~NvidiaPerformanceMonitor()
            {
                if (m_Initialized && m_Shutdown)
                    m_Shutdown();
                if (m_Module)
                    FreeLibrary(m_Module);
            }

            GpuPerformanceMetrics Query()
            {
                if (!m_Device)
                    return {};

                const auto now = std::chrono::steady_clock::now();
                if (m_LastQuery.time_since_epoch().count() != 0 &&
                    now - m_LastQuery < QueryInterval)
                {
                    return m_Metrics;
                }
                m_LastQuery = now;

                unsigned int graphicsClockMHz = 0;
                unsigned int memoryClockMHz = 0;
                unsigned int memoryBusWidthBits = 0;
                unsigned int gpuCoreCount = 0;
                if (m_GetClockInfo(m_Device, NvmlClockGraphics, &graphicsClockMHz) !=
                        NvmlSuccess ||
                    m_GetClockInfo(m_Device, NvmlClockMemory, &memoryClockMHz) !=
                        NvmlSuccess ||
                    m_GetMemoryBusWidth(m_Device, &memoryBusWidthBits) != NvmlSuccess ||
                    m_GetGpuCoreCount(m_Device, &gpuCoreCount) != NvmlSuccess)
                {
                    return m_Metrics;
                }

                NvmlUtilization utilization{};
                double gpuUtilization = m_Metrics.valid
                    ? m_Metrics.gpuUtilization
                    : 1.0;
                if (m_GetUtilizationRates &&
                    m_GetUtilizationRates(m_Device, &utilization) == NvmlSuccess)
                {
                    gpuUtilization = std::clamp(
                        double(utilization.gpu) / 100.0,
                        0.0,
                        1.0);
                }

                // NVML reports the physical memory clock. Double-data-rate
                // memory transfers twice per clock; an FP32 FMA is two FLOPs.
                m_Metrics.memoryBandwidthGBps =
                    double(memoryClockMHz) * 2.0 * double(memoryBusWidthBits) / 8000.0;
                m_Metrics.gpuGFlops =
                    double(gpuCoreCount) * 2.0 * double(graphicsClockMHz) / 1000.0;
                m_Metrics.gpuUtilization = gpuUtilization;
                m_Metrics.valid = memoryClockMHz > 0 && memoryBusWidthBits > 0 &&
                    graphicsClockMHz > 0 && gpuCoreCount > 0;
                return m_Metrics;
            }

        private:
            using NvmlDevice = void*;
            using InitFunction = int(__cdecl*)();
            using ShutdownFunction = int(__cdecl*)();
            using GetDeviceCountFunction = int(__cdecl*)(unsigned int*);
            using GetDeviceByIndexFunction = int(__cdecl*)(unsigned int, NvmlDevice*);
            using GetDeviceNameFunction = int(__cdecl*)(NvmlDevice, char*, unsigned int);
            using GetClockInfoFunction = int(__cdecl*)(
                NvmlDevice, unsigned int, unsigned int*);
            using GetUnsignedMetricFunction = int(__cdecl*)(NvmlDevice, unsigned int*);
            struct NvmlUtilization
            {
                unsigned int gpu;
                unsigned int memory;
            };
            using GetUtilizationRatesFunction = int(__cdecl*)(
                NvmlDevice, NvmlUtilization*);

            static constexpr int NvmlSuccess = 0;
            static constexpr unsigned int NvmlClockGraphics = 0;
            static constexpr unsigned int NvmlClockMemory = 2;

            template <typename FunctionType>
            FunctionType LoadFunction(const char* name) const
            {
                return reinterpret_cast<FunctionType>(GetProcAddress(m_Module, name));
            }

            HMODULE m_Module = nullptr;
            bool m_Initialized = false;
            NvmlDevice m_Device = nullptr;
            InitFunction m_Init = nullptr;
            ShutdownFunction m_Shutdown = nullptr;
            GetDeviceCountFunction m_GetDeviceCount = nullptr;
            GetDeviceByIndexFunction m_GetDeviceByIndex = nullptr;
            GetDeviceNameFunction m_GetDeviceName = nullptr;
            GetClockInfoFunction m_GetClockInfo = nullptr;
            GetUnsignedMetricFunction m_GetMemoryBusWidth = nullptr;
            GetUnsignedMetricFunction m_GetGpuCoreCount = nullptr;
            GetUtilizationRatesFunction m_GetUtilizationRates = nullptr;
            std::chrono::steady_clock::time_point m_LastQuery{};
            GpuPerformanceMetrics m_Metrics;
        };

        class IntelPerformanceMonitor
        {
        public:
            explicit IntelPerformanceMonitor(const std::string& rendererName)
            {
                if (rendererName.find("intel") == std::string::npos)
                    return;

                m_Module = LoadLibraryExW(
                    L"ControlLib.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (!m_Module)
                    return;

                m_Init = LoadFunction<InitFunction>("ctlInit");
                m_Close = LoadFunction<CloseFunction>("ctlClose");
                m_EnumerateDevices = LoadFunction<EnumerateFunction>("ctlEnumerateDevices");
                m_GetDeviceProperties = LoadFunction<GetDevicePropertiesFunction>(
                    "ctlGetDeviceProperties");
                m_EnumerateFrequencies = LoadFunction<EnumerateFunction>(
                    "ctlEnumFrequencyDomains");
                m_GetFrequencyProperties = LoadFunction<GetFrequencyPropertiesFunction>(
                    "ctlFrequencyGetProperties");
                m_GetFrequencyState = LoadFunction<GetFrequencyStateFunction>(
                    "ctlFrequencyGetState");
                m_EnumerateMemory = LoadFunction<EnumerateFunction>(
                    "ctlEnumMemoryModules");
                m_GetMemoryBandwidth = LoadFunction<GetMemoryBandwidthFunction>(
                    "ctlMemoryGetBandwidth");

                if (!m_Init || !m_Close || !m_EnumerateDevices ||
                    !m_GetDeviceProperties || !m_EnumerateFrequencies ||
                    !m_GetFrequencyProperties || !m_GetFrequencyState ||
                    !m_EnumerateMemory || !m_GetMemoryBandwidth)
                {
                    return;
                }

                CtlInitArgs initArgs{};
                initArgs.Size = sizeof(initArgs);
                initArgs.AppVersion = MakeVersion(1, 1);
                initArgs.flags = CtlInitUseLevelZero;
                if (m_Init(&initArgs, &m_Api) != CtlSuccess)
                    return;

                uint32_t deviceCount = 0;
                if (m_EnumerateDevices(m_Api, &deviceCount, nullptr) != CtlSuccess ||
                    deviceCount == 0)
                {
                    return;
                }

                std::vector<CtlHandle> devices(deviceCount);
                if (m_EnumerateDevices(m_Api, &deviceCount, devices.data()) != CtlSuccess)
                    return;

                for (CtlHandle device : devices)
                {
                    LUID adapterLuid{};
                    CtlDeviceProperties properties{};
                    properties.Size = sizeof(properties);
                    properties.Version = 3;
                    properties.pDeviceID = &adapterLuid;
                    properties.deviceIdSize = sizeof(adapterLuid);
                    if (m_GetDeviceProperties(device, &properties) != CtlSuccess ||
                        properties.deviceType != CtlGraphicsDevice ||
                        !NamesMatch(rendererName, properties.name))
                    {
                        continue;
                    }

                    m_Device = device;
                    const uint32_t vectorEngineCount =
                        properties.eusPerSubSlice *
                        properties.subSlicesPerSlice * properties.sliceCount;
                    // Arc vector engines execute 16 FP32 FLOPs per clock. The
                    // Xe-core form is equivalent at 256 FP32 FLOPs per clock.
                    m_Fp32OperationsPerClock = properties.xeCoreCount > 0
                        ? double(properties.xeCoreCount) * 256.0
                        : double(vectorEngineCount) * 16.0;
                    if ((properties.graphicsAdapterProperties &
                        CtlIntegratedAdapter) != 0)
                    {
                        m_SharedMemoryBandwidthGBps =
                            QuerySharedMemoryBandwidthGBps();
                    }
                    break;
                }

                if (!m_Device || m_Fp32OperationsPerClock <= 0.0)
                    return;

                uint32_t frequencyCount = 0;
                if (m_EnumerateFrequencies(
                        m_Device, &frequencyCount, nullptr) == CtlSuccess &&
                    frequencyCount > 0)
                {
                    std::vector<CtlHandle> frequencies(frequencyCount);
                    if (m_EnumerateFrequencies(
                            m_Device, &frequencyCount, frequencies.data()) == CtlSuccess)
                    {
                        for (CtlHandle frequency : frequencies)
                        {
                            CtlFrequencyProperties properties{};
                            properties.Size = sizeof(properties);
                            if (m_GetFrequencyProperties(frequency, &properties) ==
                                    CtlSuccess &&
                                properties.type == CtlFrequencyGpu)
                            {
                                m_GpuFrequency = frequency;
                                break;
                            }
                        }
                    }
                }

                uint32_t memoryCount = 0;
                if (m_EnumerateMemory(m_Device, &memoryCount, nullptr) == CtlSuccess &&
                    memoryCount > 0)
                {
                    m_Memory.resize(memoryCount);
                    if (m_EnumerateMemory(
                            m_Device, &memoryCount, m_Memory.data()) != CtlSuccess)
                    {
                        m_Memory.clear();
                    }
                }
            }

            ~IntelPerformanceMonitor()
            {
                if (m_Api && m_Close)
                    m_Close(m_Api);
                if (m_Module)
                    FreeLibrary(m_Module);
            }

            GpuPerformanceMetrics Query()
            {
                if (!m_Device || !m_GpuFrequency)
                    return {};

                const auto now = std::chrono::steady_clock::now();
                if (m_LastQuery.time_since_epoch().count() != 0 &&
                    now - m_LastQuery < QueryInterval)
                {
                    return m_Metrics;
                }
                m_LastQuery = now;

                CtlFrequencyState frequencyState{};
                frequencyState.Size = sizeof(frequencyState);
                if (m_GetFrequencyState(m_GpuFrequency, &frequencyState) != CtlSuccess ||
                    frequencyState.actual <= 0.0)
                {
                    return m_Metrics;
                }

                double memoryBandwidthGBps = 0.0;
                for (CtlHandle memory : m_Memory)
                {
                    CtlMemoryBandwidth bandwidth{};
                    bandwidth.Size = sizeof(bandwidth);
                    bandwidth.Version = 1;
                    if (m_GetMemoryBandwidth(memory, &bandwidth) == CtlSuccess)
                        memoryBandwidthGBps += double(bandwidth.maxBandwidth) / 1e9;
                }
                if (memoryBandwidthGBps <= 0.0)
                    memoryBandwidthGBps = m_SharedMemoryBandwidthGBps;

                m_Metrics.memoryBandwidthGBps = memoryBandwidthGBps;
                m_Metrics.gpuGFlops = m_Fp32OperationsPerClock *
                    frequencyState.actual / 1000.0;
                m_Metrics.valid = m_Metrics.memoryBandwidthGBps > 0.0 &&
                    m_Metrics.gpuGFlops > 0.0;
                return m_Metrics;
            }

        private:
            using CtlHandle = void*;
            using CtlResult = uint32_t;

            struct CtlApplicationId
            {
                uint32_t data1;
                uint16_t data2;
                uint16_t data3;
                uint8_t data4[8];
            };

            struct CtlInitArgs
            {
                uint32_t Size;
                uint8_t Version;
                uint32_t AppVersion;
                uint32_t flags;
                uint32_t SupportedVersion;
                CtlApplicationId ApplicationUid;
            };

            struct CtlFirmwareVersion
            {
                uint64_t major;
                uint64_t minor;
                uint64_t build;
            };

            struct CtlAdapterBdf
            {
                uint8_t bus;
                uint8_t device;
                uint8_t function;
            };

            struct CtlDeviceProperties
            {
                uint32_t Size;
                uint8_t Version;
                void* pDeviceID;
                uint32_t deviceIdSize;
                uint32_t deviceType;
                uint32_t supportedSubfunctionFlags;
                uint64_t driverVersion;
                CtlFirmwareVersion firmwareVersion;
                uint32_t pciVendorId;
                uint32_t pciDeviceId;
                uint32_t revisionId;
                uint32_t eusPerSubSlice;
                uint32_t subSlicesPerSlice;
                uint32_t sliceCount;
                char name[100];
                uint32_t graphicsAdapterProperties;
                uint32_t frequency;
                uint16_t pciSubsystemId;
                uint16_t pciSubsystemVendorId;
                CtlAdapterBdf adapterBdf;
                uint32_t xeCoreCount;
                char reserved[108];
            };

            struct CtlFrequencyProperties
            {
                uint32_t Size;
                uint8_t Version;
                uint32_t type;
                bool canControl;
                double minimum;
                double maximum;
            };

            struct CtlFrequencyState
            {
                uint32_t Size;
                uint8_t Version;
                double currentVoltage;
                double request;
                double tdp;
                double efficient;
                double actual;
                uint32_t throttleReasons;
            };

            struct CtlMemoryBandwidth
            {
                uint32_t Size;
                uint8_t Version;
                uint64_t maxBandwidth;
                uint64_t timestamp;
                uint64_t readCounter;
                uint64_t writeCounter;
            };

            static_assert(sizeof(CtlInitArgs) == 36);
            static_assert(sizeof(CtlDeviceProperties) == 320);

            using InitFunction = CtlResult(__cdecl*)(CtlInitArgs*, CtlHandle*);
            using CloseFunction = CtlResult(__cdecl*)(CtlHandle);
            using EnumerateFunction = CtlResult(__cdecl*)(
                CtlHandle, uint32_t*, CtlHandle*);
            using GetDevicePropertiesFunction = CtlResult(__cdecl*)(
                CtlHandle, CtlDeviceProperties*);
            using GetFrequencyPropertiesFunction = CtlResult(__cdecl*)(
                CtlHandle, CtlFrequencyProperties*);
            using GetFrequencyStateFunction = CtlResult(__cdecl*)(
                CtlHandle, CtlFrequencyState*);
            using GetMemoryBandwidthFunction = CtlResult(__cdecl*)(
                CtlHandle, CtlMemoryBandwidth*);

            static constexpr CtlResult CtlSuccess = 0;
            static constexpr uint32_t CtlInitUseLevelZero = 1u;
            static constexpr uint32_t CtlGraphicsDevice = 1u;
            static constexpr uint32_t CtlIntegratedAdapter = 1u;
            static constexpr uint32_t CtlFrequencyGpu = 0u;

            static constexpr uint32_t MakeVersion(uint32_t major, uint32_t minor)
            {
                return (major << 16u) | minor;
            }

            template <typename FunctionType>
            FunctionType LoadFunction(const char* name) const
            {
                return reinterpret_cast<FunctionType>(GetProcAddress(m_Module, name));
            }

            HMODULE m_Module = nullptr;
            CtlHandle m_Api = nullptr;
            CtlHandle m_Device = nullptr;
            CtlHandle m_GpuFrequency = nullptr;
            std::vector<CtlHandle> m_Memory;
            double m_Fp32OperationsPerClock = 0.0;
            double m_SharedMemoryBandwidthGBps = 0.0;
            InitFunction m_Init = nullptr;
            CloseFunction m_Close = nullptr;
            EnumerateFunction m_EnumerateDevices = nullptr;
            GetDevicePropertiesFunction m_GetDeviceProperties = nullptr;
            EnumerateFunction m_EnumerateFrequencies = nullptr;
            GetFrequencyPropertiesFunction m_GetFrequencyProperties = nullptr;
            GetFrequencyStateFunction m_GetFrequencyState = nullptr;
            EnumerateFunction m_EnumerateMemory = nullptr;
            GetMemoryBandwidthFunction m_GetMemoryBandwidth = nullptr;
            std::chrono::steady_clock::time_point m_LastQuery{};
            GpuPerformanceMetrics m_Metrics;
        };
    }

    GpuPerformanceMetrics QueryGpuPerformanceMetrics(const char* rendererName)
    {
        const std::string normalizedRendererName = NormalizeName(
            rendererName ? rendererName : "");
        if (normalizedRendererName.find("nvidia") != std::string::npos)
        {
            static NvidiaPerformanceMonitor monitor(normalizedRendererName);
            return monitor.Query();
        }
        if (normalizedRendererName.find("intel") != std::string::npos)
        {
            static IntelPerformanceMonitor monitor(normalizedRendererName);
            return monitor.Query();
        }
        return {};
    }
}
