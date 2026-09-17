#include "renderer_gpu_fixture.h"

#include <stb_image.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const char* referenceDirectory = nullptr;
    constexpr size_t MaxRecordBytes = 16 * 1024 * 1024;
    void Require(bool value, const char* reason) noexcept
    {
        if (value) return;
        fprintf(stderr, "captured GPU reference failed: %s\n", reason); exit(1);
    }
}

void SetRendererGpuReferenceDirectory(const char* directory) noexcept
{ Require(directory && *directory && !referenceDirectory, "reference directory"); referenceDirectory = directory; }

RendererGpuReference::RendererGpuReference(const char* name) noexcept
{
    const uint32_t endian = 1;
    Require(*reinterpret_cast<const uint8_t*>(&endian) == 1, "little-endian fixture");
    Require(referenceDirectory != nullptr, "reference directory is set");
    char path[4096];
    const int size = snprintf(path, sizeof(path), "%s/%s", referenceDirectory, name);
    Require(size > 0 && size_t(size) < sizeof(path), "reference path capacity");
    m_file = fopen(path, "rb"); Require(m_file != nullptr, "open reference");
    char magic[8];
    Require(fread(magic, 1, sizeof(magic), m_file) == sizeof(magic) && !memcmp(magic, "UVGZ0001", 8), "reference version");
}
RendererGpuReference::~RendererGpuReference()
{ if (m_file) fclose(m_file); }
uint32_t RendererGpuReference::U32() noexcept
{ uint32_t value; Require(fread(&value, 1, 4, m_file) == 4, "complete record header"); return value; }
void RendererGpuReference::Read(void* expected, size_t size) noexcept
{
    Require(m_file && expected && size && size <= MaxRecordBytes, "bounded expected record");
    Require(U32() == size, "exact record byte count");
    const uint32_t compressedBytes = U32();
    Require(compressedBytes && compressedBytes <= size + 65536, "bounded compressed record");
    auto* compressed = static_cast<char*>(malloc(compressedBytes));
    Require(compressed != nullptr, "compressed record allocation");
    Require(fread(compressed, 1, compressedBytes, m_file) == compressedBytes, "complete compressed record");
    const int decoded = stbi_zlib_decode_buffer(static_cast<char*>(expected), int(size), compressed, int(compressedBytes));
    free(compressed);
    Require(decoded == int(size), "complete decoded record"); ++m_records;
}
void RendererGpuReference::Match(const void* actual, size_t size) noexcept
{
    Require(actual && size && size <= MaxRecordBytes, "bounded comparison");
    void* expected = malloc(size); Require(expected != nullptr, "comparison allocation");
    Read(expected, size);
    const bool same = memcmp(expected, actual, size) == 0;
    free(expected); Require(same, "exact reference bytes");
}
void RendererGpuReference::Finish(uint32_t records) noexcept
{
    Require(m_file && m_records == records && U32() == 0 && U32() == records, "complete reference sequence");
    Require(fgetc(m_file) == EOF && !ferror(m_file) && fclose(m_file) == 0, "exact reference end");
    m_file = nullptr;
}
