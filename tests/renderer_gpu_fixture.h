#pragma once

#include <stdio.h>
#include <stdint.h>

void SetRendererGpuReferenceDirectory(const char* directory) noexcept;

// fixed test records, compressed with the already retained image codec's zlib reader.
class RendererGpuReference final
{
public:
    explicit RendererGpuReference(const char* name) noexcept;
    ~RendererGpuReference();
    RendererGpuReference(const RendererGpuReference&) = delete;
    RendererGpuReference& operator=(const RendererGpuReference&) = delete;

    void Read(void* expected, size_t size) noexcept;
    void Match(const void* actual, size_t size) noexcept;
    void Finish(uint32_t records) noexcept;

private:
    uint32_t U32() noexcept;
    FILE* m_file = nullptr;
    uint32_t m_records = 0;
};
