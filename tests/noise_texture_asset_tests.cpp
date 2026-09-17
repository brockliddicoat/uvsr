#include "file_bytes.h"
#include "noise_texture_asset.h"
#include "windows_executable_path.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool value, const char* message) noexcept
    {
        ++checks;
        if (!value) { ++failures; fprintf(stderr, "failed: %s\n", message); }
    }
    bool Preserved(const uvsr::FileBytes& bytes, const char* prior, const char* source) noexcept
    {
        return bytes.Data() == prior && bytes.Size() == 127 &&
            !memcmp(bytes.Data(), source, 127) && bytes.Data()[127] == '\0';
    }
}

bool TestNoiseAssetOwnership(const wchar_t* directory, const wchar_t* small,
    const wchar_t* empty, const wchar_t* missing, const char* source) noexcept
{
    using namespace uvsr;
    FileBytes bytes;
    FileReadResult result;
    uint64_t measured = UINT64_MAX;
    constexpr uint64_t mismatchSizes[] = {0, 126, 128};
    constexpr size_t failurePoints[] = {0, 1};
    Check(ReadFileBytesExact(small, 127, bytes, result, measured) && measured == 127 &&
        bytes.Size() == 127 && !memcmp(bytes.Data(), source, 127), "exact reader publishes all bytes");
    const char* prior = bytes.Data();
    for (uint64_t expected : mismatchSizes)
    {
        measured = UINT64_MAX;
        Check(!ReadFileBytesExact(small, expected, bytes, result, measured) &&
            result.error == FileReadError::SizeMismatch && result.systemCode == 0 && measured == 127 &&
            Preserved(bytes, prior, source), "size mismatch preserves output and actual measurement");
    }
    FailFileAllocationAfter(0);
    Check(!ReadFileBytesExact(small, 126, bytes, result, measured) && result.error == FileReadError::SizeMismatch,
        "size mismatch precedes allocation");
    Check(!ReadFileBytesExact(small, 127, bytes, result, measured) && result.error == FileReadError::OutOfMemory &&
        measured == 127 && Preserved(bytes, prior, source), "mismatch does not consume allocation failure");
    ClearFileAllocationFailure();
    const wchar_t* unmeasuredPaths[] = {nullptr, L"", missing, directory, L"NUL"};
    for (const wchar_t* path : unmeasuredPaths)
    {
        measured = UINT64_MAX;
        Check(!ReadFileBytesExact(path, 127, bytes, result, measured) && measured == 0 &&
            Preserved(bytes, prior, source), "unmeasured failure resets size and preserves bytes");
    }
    for (size_t transfers : failurePoints)
    {
        SetFileReadTestLimits(17, transfers, false);
        Check(!ReadFileBytesExact(small, 127, bytes, result, measured) && result.error == FileReadError::Read &&
            measured == 127 && Preserved(bytes, prior, source), "failed exact transfer retains size and previous bytes");
        ClearFileReadTestLimits();
    }
    SetFileReadTestLimits(0, SIZE_MAX, false);
    Check(!ReadFileBytesExact(small, 127, bytes, result, measured) && result.error == FileReadError::Read &&
        measured == 127 && Preserved(bytes, prior, source), "exact zero-progress read preserves output");
    ClearFileReadTestLimits();
    SetFileReadTestLimits(SIZE_MAX, SIZE_MAX, true);
    Check(!ReadFileBytesExact(small, 127, bytes, result, measured) && result.error == FileReadError::Close &&
        measured == 127 && Preserved(bytes, prior, source), "exact close failure preserves output");
    ClearFileReadTestLimits();
    Check(!ReadFileBytesExact(empty, 1, bytes, result, measured) && result.error == FileReadError::SizeMismatch &&
        measured == 0 && Preserved(bytes, prior, source), "empty mismatch precedes publication");
    SetFileReadTestLimits(17, SIZE_MAX, false);
    Check(ReadFileBytesExact(small, 127, bytes, result, measured) && measured == 127 &&
        bytes.Size() == 127 && !memcmp(bytes.Data(), source, 127), "exact partial transfers complete");
    ClearFileReadTestLimits();
    Check(ReadFileBytesExact(empty, 0, bytes, result, measured) && measured == 0 && bytes.Size() == 0,
        "exact empty read succeeds");

    const NoiseTextureAsset seed(directory, "small.bin", 127);
    Check(seed.Result().error == NoiseTextureAssetError::None && seed.Size() == 127 &&
        !memcmp(seed.Data(), source, 127) && *seed.PathText(), "asset owns complete bytes and diagnostic path");
    const char* savedBytes = seed.Data();
    const char* savedPath = seed.PathText();
    for (uint64_t expected : mismatchSizes)
    {
        const NoiseTextureAsset asset(directory, "small.bin", expected);
        Check(asset.Result().error == NoiseTextureAssetError::Size && asset.Result().measuredBytes == 127 &&
            asset.Size() == 0 && !strcmp(asset.PathText(), seed.PathText()), "asset size failure retains diagnostic text only");
    }
    {
        const NoiseTextureAsset asset(directory, "empty.bin", 0);
        Check(asset.Result().error == NoiseTextureAssetError::None && asset.Size() == 0 && *asset.PathText(), "empty CPU asset is valid");
    }
    {
        const NoiseTextureAsset asset(directory, "missing.bin", 127);
        Check(asset.Result().error == NoiseTextureAssetError::Open && asset.Size() == 0 && *asset.PathText(), "missing asset retains diagnostic text");
    }
    const char* invalidNames[] = {nullptr, "", "\xff", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"};
    for (const char* name : invalidNames)
    {
        const NoiseTextureAsset asset(directory, name, 127);
        Check(asset.Result().error == NoiseTextureAssetError::Path && asset.Size() == 0, "invalid catalog filename is checked without truncation");
    }
    {
        const NoiseTextureAsset asset(nullptr, "small.bin", 127);
        Check(asset.Result().error == NoiseTextureAssetError::Path && asset.Size() == 0, "null directory is checked");
    }
    FailWindowsPathOnce(WindowsPathError::Allocation);
    {
        const NoiseTextureAsset asset(directory, "small.bin", 127);
        Check(asset.Result().error == NoiseTextureAssetError::Allocation && asset.Size() == 0, "joined path allocation failure is checked");
    }
    for (size_t failure : failurePoints)
    {
        FailWindowsPathTextAllocationAfter(failure);
        const NoiseTextureAsset asset(directory, "small.bin", 127);
        Check(asset.Result().error == NoiseTextureAssetError::Allocation && asset.Size() == 0, "generic path scratch or text allocation failure is checked");
        ClearWindowsPathTextAllocationFailure();
    }
    FailFileAllocationAfter(0);
    {
        const NoiseTextureAsset asset(directory, "small.bin", 127);
        Check(asset.Result().error == NoiseTextureAssetError::Allocation && asset.Result().measuredBytes == 127 &&
            asset.Size() == 0 && !strcmp(asset.PathText(), seed.PathText()), "asset byte allocation failure retains path and size");
    }
    ClearFileAllocationFailure();
    for (size_t transfers : failurePoints)
    {
        SetFileReadTestLimits(17, transfers, false);
        const NoiseTextureAsset asset(directory, "small.bin", 127);
        Check(asset.Result().error == NoiseTextureAssetError::Read && asset.Result().measuredBytes == 127 &&
            asset.Size() == 0 && !strcmp(asset.PathText(), seed.PathText()), "asset partial read failure retains path and size");
        ClearFileReadTestLimits();
    }
    SetFileReadTestLimits(SIZE_MAX, SIZE_MAX, true);
    {
        const NoiseTextureAsset asset(directory, "small.bin", 127);
        Check(asset.Result().error == NoiseTextureAssetError::Read && asset.Result().measuredBytes == 127 &&
            asset.Size() == 0 && !strcmp(asset.PathText(), seed.PathText()), "asset close failure publishes no bytes");
    }
    ClearFileReadTestLimits();
    Check(seed.Data() == savedBytes && seed.PathText() == savedPath && seed.Size() == 127 &&
        !memcmp(seed.Data(), source, 127), "independent failures cannot invalidate a live asset");
    printf("noise/exact-file ownership checks: %u, failures: %u\n", checks, failures);
    return failures == 0;
}
