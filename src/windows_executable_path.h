#pragma once

#include <Windows.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace uvsr
{
    inline std::filesystem::path ExecutableDirectoryFromModulePath(
        std::wstring_view modulePath)
    {
        const std::filesystem::path executablePath(modulePath);
        const std::filesystem::path directory = executablePath.parent_path();
        if (modulePath.empty() || directory.empty())
            throw std::runtime_error("UVSR could not determine its executable directory.");
        return directory;
    }

    inline std::filesystem::path GetExecutableDirectoryWide()
    {
        std::wstring modulePath(32768, L'\0');
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(
            nullptr,
            modulePath.data(),
            static_cast<DWORD>(modulePath.size()));
        if (length == 0 || length >= modulePath.size())
            throw std::runtime_error("UVSR could not identify its executable path.");
        modulePath.resize(length);
        return ExecutableDirectoryFromModulePath(modulePath);
    }
}
