#include "renderer_shader_factory_nvrhi.h"

#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace uvsr
{
    struct RendererShaderFactoryTestAccess
    {
        static void FailAfter(size_t count) noexcept { RendererShaderFactory::FailAllocationAfter(count); }
        static bool Select(RendererShaderFactory& factory, const char* file, const char* entry,
            ArrayView<const shader_blob::Constant> constants, const void*& bytes, size_t& size) noexcept
        {
            return factory.SelectBytecode(file, entry, constants, bytes, size);
        }
    };
}

namespace
{
    using namespace uvsr;
    using Access = RendererShaderFactoryTestAccess;
    const wchar_t* directory = nullptr;
    unsigned checks = 0;
    unsigned failures = 0;
    void Check(bool value, const char* name) noexcept
    {
        ++checks;
        if (!value) { ++failures; fprintf(stderr, "failed: %s\n", name); }
    }
    bool Write(const wchar_t* name, const void* bytes, size_t size) noexcept
    {
        wchar_t path[32768];
        if (swprintf_s(path, L"%ls/%ls", directory, name) < 0 || size > MAXDWORD) return false;
        const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        const bool result = WriteFile(file, bytes, DWORD(size), &written, nullptr) && written == size;
        return CloseHandle(file) && result;
    }
    bool Selected(RendererShaderFactory& factory, const char* file, const char* expected,
        const char* entry = "main", ArrayView<const shader_blob::Constant> constants = {}) noexcept
    {
        const void* bytes = nullptr;
        size_t size = 0;
        return Access::Select(factory, file, entry, constants, bytes, size) &&
            size == strlen(expected) && !memcmp(bytes, expected, size);
    }
    bool Rejected(RendererShaderFactory& factory, const char* file, const char* entry = "main",
        ArrayView<const shader_blob::Constant> constants = {}) noexcept
    {
        const char sentinel = 'x';
        const void* bytes = &sentinel;
        size_t size = 913;
        return !Access::Select(factory, file, entry, constants, bytes, size) && bytes == &sentinel && size == 913;
    }
    struct Blob
    {
        unsigned char bytes[512] = {'N','V','S','P'};
        size_t size = 4;
        bool Append(const char* key, const char* data) noexcept
        {
            const size_t k = strlen(key), d = strlen(data);
            if (size > sizeof(bytes) - 8 || k > sizeof(bytes) - size - 8 || d > sizeof(bytes) - size - 8 - k)
                return false;
            for (unsigned i = 0; i < 4; ++i)
            {
                bytes[size + i] = static_cast<unsigned char>(k >> (8 * i));
                bytes[size + 4 + i] = static_cast<unsigned char>(d >> (8 * i));
            }
            size += 8;
            memcpy(bytes + size, key, k); size += k;
            memcpy(bytes + size, data, d); size += d;
            return true;
        }
    };
    void CheckCodec() noexcept
    {
        Blob blob;
        Check(blob.Append("ALPHA=1 BETA=2", "first") && blob.Append("ALPHA=2 BETA=1", "second"), "fixture capacity");
        const shader_blob::Constant constants[] = {{"BETA","2"},{"ALPHA","1"}};
        const char sentinel = 'x';
        const void* bytes = &sentinel;
        size_t size = 91;
        Check(shader_blob::find_permutation(blob.bytes, blob.size, constants, 2, &bytes, &size) &&
            size == 5 && !memcmp(bytes, "first", 5), "unsorted borrowed defines");
        const auto reject = [&](const void* data, size_t count, const shader_blob::Constant* key, uint32_t keyCount)
        {
            bytes = &sentinel; size = 91;
            return !shader_blob::find_permutation(data, count, key, keyCount, &bytes, &size) &&
                bytes == &sentinel && size == 91;
        };
        Check(reject(nullptr, blob.size, constants, 2), "null blob");
        Check(reject(blob.bytes, 0, nullptr, 0), "empty raw blob");
        Check(reject(blob.bytes, size_t(PTRDIFF_MAX) + 1, constants, 2), "oversized blob view");
        Check(reject(blob.bytes, blob.size, nullptr, 2), "null define view");
        shader_blob::Constant invalid[] = {{nullptr,"1"}};
        Check(reject(blob.bytes, blob.size, invalid, 1), "null define name");
        invalid[0] = {"ALPHA",nullptr};
        Check(reject(blob.bytes, blob.size, invalid, 1), "null define value");
        Blob one;
        Check(one.Append("MODE=0", "data"), "single fixture");
        const shader_blob::Constant mode[] = {{"MODE","0"}};
        for (size_t length = 4; length < one.size; ++length)
            Check(reject(one.bytes, length, mode, 1), "truncated blob preserves output");
        auto bad = one;
        bad.bytes[bad.size++] = 0;
        Check(reject(bad.bytes, bad.size, mode, 1), "malformed tail after matching entry");
        bad = one; memset(bad.bytes + 4, 255, 4);
        Check(reject(bad.bytes, bad.size, mode, 1), "key length exceeds blob");
        bad = one; memset(bad.bytes + 8, 255, 4);
        Check(reject(bad.bytes, bad.size, mode, 1), "payload length exceeds blob");
        bad = one; memset(bad.bytes + 8, 0, 4);
        Check(reject(bad.bytes, bad.size, mode, 1), "zero payload");
        Blob repeated;
        const shader_blob::Constant duplicate[] = {{"B","2"},{"A","0"},{"B","1"}};
        Check(repeated.Append("A=0 B=2 B=1", "stable"), "duplicate-name fixture");
        Check(shader_blob::find_permutation(repeated.bytes, repeated.size, duplicate, 3, &bytes, &size) &&
            size == 6 && !memcmp(bytes,"stable",6), "stable duplicate-name ordering");
        Check(reject("raw", 3, mode, 1), "raw blob rejects nondefault defines");
        Check(shader_blob::find_permutation("raw", 3, nullptr, 0, &bytes, &size) && size == 3 &&
            !memcmp(bytes, "raw", 3), "raw default bytecode");
    }
    void CheckFiles() noexcept
    {
        Check(Write(L"sample.bin", "first", 5), "write raw fixture");
        RendererShaderFactory factory(nullptr, directory);
        Check(Selected(factory, "uvsr/sample.hlsl", "first"), "initial file load");
        Check(Write(L"sample.bin", "second", 6), "rewrite cached file");
        Access::FailAfter(0);
        Check(Selected(factory, "uvsr/sample.hlsl", "first"), "cache hit needs no allocation");
        Access::FailAfter(SIZE_MAX);
        factory.ClearCache();
        Check(Selected(factory, "uvsr/sample.hlsl", "second"), "explicit reload refreshes bytes");
        for (const char* path : {"uvsr/./sample.hlsl", "uvsr/../sample.hlsl", "uvsr\\sample.hlsl",
                "framework/sample.hlsl", "uvsr/sample.bin", "uvsr/C:sample.hlsl", "uvsr/nested/sample.hlsl"})
            Check(Rejected(factory, path), "invalid logical path");
        for (const char* entry : {"", "../x", "1main", "has space"})
            Check(Rejected(factory, "uvsr/sample.hlsl", entry), "invalid entry name");
        Check(Write(L"name_Run.bin", "named", 5), "write named entry");
        Check(Selected(factory,"uvsr/name.hlsl","named","Run"), "entry suffix");
        Access::FailAfter(0);
        Check(Selected(factory,"uvsr/name_Run.hlsl","named"), "equal blob paths share cache");
        Access::FailAfter(SIZE_MAX);
        Check(Write(L"sch\u00f6n.bin", "unicode", 7), "write unicode leaf");
        Check(Selected(factory,u8"uvsr/sch\u00f6n.hlsl","unicode"), "UTF-8 logical filename");
        Check(Write(L"empty.bin", "", 0) && Rejected(factory,"uvsr/empty.hlsl"), "empty file rejection");
        Check(Write(L"empty.bin", "retry", 5) && Selected(factory,"uvsr/empty.hlsl","retry"), "failed file read can retry");
        Check(Rejected(factory,"uvsr/missing.hlsl"), "missing file rejection");
        Check(Write(L"missing.bin", "retry", 5) && Selected(factory,"uvsr/missing.hlsl","retry"), "missing file can retry");
        Blob blob;
        Check(blob.Append("ALPHA=1 BETA=2", "selected") && Write(L"packed.bin", blob.bytes, blob.size), "write packed fixture");
        const shader_blob::Constant key[] = {{"BETA","2"},{"ALPHA","1"}};
        Check(Selected(factory,"uvsr/packed.hlsl","selected","main",key), "cached permutation selection");
        const shader_blob::Constant missing[] = {{"ALPHA","3"}};
        Check(Rejected(factory,"uvsr/packed.hlsl","main",missing), "missing permutation preserves output");
        for (size_t failure = 0; failure < 3; ++failure)
        {
            factory.ClearCache();
            Access::FailAfter(failure);
            Check(Rejected(factory,"uvsr/sample.hlsl"), "file allocation failure preserves output");
            Access::FailAfter(SIZE_MAX);
            Check(Selected(factory,"uvsr/sample.hlsl","second"), "allocation failure permits retry");
        }
        Access::FailAfter(0);
        {
            RendererShaderFactory rejected(nullptr, directory);
            Check(Rejected(rejected,"uvsr/sample.hlsl"), "directory allocation failure");
        }
        Access::FailAfter(SIZE_MAX);
        Check(!factory.CreateShader("uvsr/sample.hlsl","main",{},nvrhi::ShaderType::Compute), "device is required for GPU shader");
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;
    wchar_t scratch[32768];
    if (swprintf_s(scratch, L"%ls/case-%lu-%llu", argv[1], GetCurrentProcessId(),
            static_cast<unsigned long long>(GetTickCount64())) < 0 || !CreateDirectoryW(scratch, nullptr)) return 2;
    directory = scratch;
    CheckCodec();
    CheckFiles();
    printf("shader bytecode/file checks: %u, failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
