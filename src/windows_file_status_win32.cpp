#include "windows_file_status.h"
#include <Windows.h>

namespace uvsr
{
    WindowsFileStatus QueryWindowsRegularFile(const wchar_t* path) noexcept
    {
        if (!path) return {false, ERROR_INVALID_PARAMETER, 0};
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data))
        {
            const DWORD code = GetLastError();
            if (code != ERROR_SHARING_VIOLATION) return {false, code, 0};
            WIN32_FIND_DATAW found{};
            const HANDLE search = FindFirstFileW(path, &found);
            if (search == INVALID_HANDLE_VALUE) return {false, GetLastError(), 0};
            if (!FindClose(search)) return {false, 0, GetLastError()};
            data.dwFileAttributes = found.dwFileAttributes;
        }
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return {!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), 0, 0};

        const HANDLE file = CreateFileW(path, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (file == INVALID_HANDLE_VALUE) return {false, GetLastError(), 0};
        FILE_BASIC_INFO basic{};
        const bool queried = GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)) != FALSE;
        const DWORD queryCode = queried ? ERROR_SUCCESS : GetLastError();
        const DWORD closeCode = CloseHandle(file) ? ERROR_SUCCESS : GetLastError();
        return {queried && !closeCode && !(basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY), queryCode, closeCode};
    }

    WindowsErrorMessage::WindowsErrorMessage(uint32_t code) noexcept
    {
        constexpr DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        DWORD count = 0;
        for (unsigned attempt = 0; attempt < 3 && !count; ++attempt)
        {
            DWORD language = 0;
            if (!attempt) language = 0x0409;
            else if (attempt == 1 && !GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT,
                    LOCALE_ILANGUAGE | LOCALE_RETURN_NUMBER, reinterpret_cast<wchar_t*>(&language),
                    sizeof(language) / sizeof(wchar_t)))
                continue;
            count = FormatMessageA(flags, nullptr, code, language, reinterpret_cast<char*>(&m_Text), 0, nullptr);
        }
        while (count)
        {
            const char last = m_Text[count - 1];
            if (last != ' ' && last != '\r' && last != '\n' && last != '\t' && last != '\0') break;
            --count;
        }
        if (count) m_Text[count] = '\0';
        m_Size = count;
    }
    WindowsErrorMessage::~WindowsErrorMessage() noexcept { LocalFree(m_Text); }
}
