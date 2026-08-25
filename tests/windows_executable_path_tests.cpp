#include <cassert>
#include <filesystem>
#include <stdexcept>

#include "windows_executable_path.h"

int main()
{
    const std::filesystem::path expected(
        L"C:\\Users\\Miyuki-\u7F8E\u96EA\\AppData\\Local\\Programs\\UVSR\\bin");
    const std::filesystem::path actual =
        uvsr::ExecutableDirectoryFromModulePath(
            L"C:\\Users\\Miyuki-\u7F8E\u96EA\\AppData\\Local\\Programs\\UVSR\\bin\\uvsr-engine.exe");
    assert(actual == expected);

    const std::filesystem::path runningDirectory =
        uvsr::GetExecutableDirectoryWide();
    assert(runningDirectory.is_absolute());

    bool rejectedEmptyPath = false;
    try
    {
        (void)uvsr::ExecutableDirectoryFromModulePath(L"");
    }
    catch (const std::runtime_error&)
    {
        rejectedEmptyPath = true;
    }
    assert(rejectedEmptyPath);
    return 0;
}
