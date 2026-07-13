#pragma once

namespace uvsr
{
    struct GpuPerformanceMetrics
    {
        double memoryBandwidthGBps = 0.0;
        double gpuGFlops = 0.0;
        bool valid = false;
    };

    GpuPerformanceMetrics QueryGpuPerformanceMetrics(const char* rendererName);
}
