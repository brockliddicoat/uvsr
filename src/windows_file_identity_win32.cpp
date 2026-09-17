#include "windows_file_identity.h"

#include <Windows.h>
#include <string.h>

namespace uvsr
{
    namespace
    {
        DWORD ReadIdentity(HANDLE handle, FILE_ID_INFO& identity) noexcept
        {
            if (GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity))) return ERROR_SUCCESS;
            const DWORD code = GetLastError();
            if (code != ERROR_NOT_SUPPORTED && code != ERROR_INVALID_PARAMETER) return code;
            BY_HANDLE_FILE_INFORMATION fallback;
            if (!GetFileInformationByHandle(handle, &fallback)) return GetLastError();
            identity.VolumeSerialNumber = fallback.dwVolumeSerialNumber;
            memcpy(identity.FileId.Identifier, &fallback.nFileIndexHigh, 4);
            memcpy(identity.FileId.Identifier + 4, &fallback.nFileIndexLow, 4);
            memset(identity.FileId.Identifier + 8, 0, 8);
            return ERROR_SUCCESS;
        }

        HANDLE OpenIdentity(const wchar_t* path) noexcept
        {
            return CreateFileW(path, FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        }

        void CloseIdentity(HANDLE handle, WindowsSameFileResult& result, bool& failed) noexcept
        {
            if (handle != INVALID_HANDLE_VALUE && !CloseHandle(handle))
            {
                const DWORD code = GetLastError();
                if (!failed) result.cleanupCode = code;
                failed = true;
            }
        }
    }

    bool QueryWindowsSameFile(const wchar_t* left, const wchar_t* right,
        WindowsSameFileResult& result) noexcept
    {
        result = {};
        if (!left || !right)
        {
            result.nativeCode = ERROR_INVALID_PARAMETER;
            return false;
        }
        HANDLE leftHandle = OpenIdentity(left);
        if (leftHandle == INVALID_HANDLE_VALUE)
        {
            result.nativeCode = GetLastError();
            return false;
        }
        FILE_ID_INFO leftIdentity{};
        result.nativeCode = ReadIdentity(leftHandle, leftIdentity);
        HANDLE rightHandle = INVALID_HANDLE_VALUE;
        if (!result.nativeCode)
        {
            rightHandle = OpenIdentity(right);
            if (rightHandle == INVALID_HANDLE_VALUE) result.nativeCode = GetLastError();
            else
            {
                FILE_ID_INFO rightIdentity{};
                result.nativeCode = ReadIdentity(rightHandle, rightIdentity);
                if (!result.nativeCode)
                    result.equivalent = leftIdentity.VolumeSerialNumber == rightIdentity.VolumeSerialNumber &&
                        memcmp(leftIdentity.FileId.Identifier, rightIdentity.FileId.Identifier, sizeof(leftIdentity.FileId.Identifier)) == 0;
            }
        }
        bool cleanupFailed = false;
        CloseIdentity(rightHandle, result, cleanupFailed);
        CloseIdentity(leftHandle, result, cleanupFailed);
        if (result.nativeCode || cleanupFailed)
        {
            result.equivalent = false;
            return false;
        }
        return true;
    }
}
