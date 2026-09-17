#pragma once

#include <stddef.h>
#include <stdint.h>

namespace uvsr
{
    struct FileWriteSpan
    {
        const void* data = nullptr;
        size_t size = 0;
    };
    enum class FileWriteError : uint8_t
    {
        None, InvalidPath, InvalidInput, TooLarge, OutOfMemory,
        Directory, Create, Write, Flush, Close, Publish
    };
    struct FileWriteResult
    {
        FileWriteError error = FileWriteError::None;
        uint32_t systemCode = 0;
        uint32_t cleanupCode = 0;
    };

    // path is a terminated full destination filename. success means its parent
    // is ready, including when it already existed. no file is opened here.
    [[nodiscard]] bool PrepareFileParentDirectories(const wchar_t* path,
        FileWriteResult& result) noexcept;

    // spans borrow readable storage through this synchronous call. publication
    // replaces the destination only after every chunk, flush and close succeeds.
    // existing temporary siblings are preserved. failed cleanup is reported.
    [[nodiscard]] bool WriteFileBytesAtomically(const wchar_t* path,
        const FileWriteSpan* spans, size_t spanCount, FileWriteResult& result) noexcept;

#if defined(UVSR_FILE_WRITE_TEST_HOOKS)
    void FailFileWriteAllocationAfter(size_t successfulAllocations) noexcept;
    void SetFileWriteTestLimits(size_t maximumTransfer, size_t successfulTransfers,
        bool failFlush, bool failClose, bool failPublish) noexcept;
    void ClearFileWriteTestFailures() noexcept;
#endif
}
