#pragma once

#include <stddef.h>
#include <stdint.h>

namespace uvsr
{
    enum class FileReadError : uint8_t
    {
        None, InvalidPath, Missing, NotRegular, TooLarge, OutOfMemory, Open, Measure, Read, Close, SizeMismatch
    };
    struct FileReadResult
    {
        FileReadError error = FileReadError::None;
        uint32_t systemCode = 0;
    };

    // read-only bytes remain borrowed until this owner moves, clears or dies.
    // the extra terminator is not included in Size(). failed reads preserve it.
    class FileBytes
    {
    public:
        FileBytes() noexcept = default;
        ~FileBytes() noexcept;
        FileBytes(const FileBytes&) = delete;
        FileBytes& operator=(const FileBytes&) = delete;
        FileBytes(FileBytes&& other) noexcept;
        FileBytes& operator=(FileBytes&& other) noexcept;
        [[nodiscard]] const char* Data() const noexcept { return m_Data ? m_Data : ""; }
        [[nodiscard]] size_t Size() const noexcept { return m_Size; }
        void Clear() noexcept;
    private:
        char* m_Data = nullptr;
        size_t m_Size = 0;
        friend struct FileReader;
    };

    [[nodiscard]] bool ReadFileBytes(const wchar_t* path, uint64_t maximumBytes,
        FileBytes& output, FileReadResult& result) noexcept;
    // reject a mismatch before allocation, through the same handle. measuredBytes
    // is zero until measurement succeeds, then retains that size even on failure.
    [[nodiscard]] bool ReadFileBytesExact(const wchar_t* path, uint64_t expectedBytes,
        FileBytes& output, FileReadResult& result, uint64_t& measuredBytes) noexcept;
    [[nodiscard]] bool ReadFileBytesUtf8(const char* path, uint64_t maximumBytes,
        FileBytes& output, FileReadResult& result) noexcept;

#if defined(UVSR_FILE_BYTES_TEST_HOOKS)
    void FailFileAllocationAfter(size_t successfulAllocations) noexcept;
    void ClearFileAllocationFailure() noexcept;
    void SetFileReadTestLimits(size_t maximumTransfer, size_t successfulTransfers, bool failClose) noexcept;
    void ClearFileReadTestLimits() noexcept;
#endif
}
