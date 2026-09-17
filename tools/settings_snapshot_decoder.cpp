#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "settings_snapshot_decoder.h"

#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string_view>

namespace
{
    void PrintUsage()
    {
        fputs("usage: uvsr-settings-snapshot <code> [--catalog <path>] [--json]\n", stderr);
    }
    bool AppendPath(const char* text, uvsr::SettingsSnapshotCatalogPaths& catalogs,
        uvsr::SettingsSnapshotError& error)
    {
        const size_t length = strlen(text);
        if (length >= INT_MAX)
        {
            error.code = uvsr::SettingsSnapshotErrorCode::Capacity;
            error.message = "snapshot catalog path capacity overflow";
            return false;
        }
        const UINT codePage = ___lc_codepage_func() == CP_UTF8 ? CP_UTF8
            : AreFileApisANSI() ? CP_ACP : CP_OEMCP;
        const int size = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, text, int(length + 1), nullptr, 0);
        if (!size)
        {
            error.code = uvsr::SettingsSnapshotErrorCode::Path;
            error.message = "cannot convert snapshot catalog path";
            error.nativeCode = GetLastError();
            return false;
        }
        wchar_t* path = static_cast<wchar_t*>(malloc(size_t(size) * sizeof(wchar_t)));
        if (!path)
        {
            error.code = uvsr::SettingsSnapshotErrorCode::OutOfMemory;
            error.message = "cannot allocate snapshot catalog path";
            return false;
        }
        const bool converted = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, text, int(length + 1), path, size) == size;
        if (!converted)
        {
            error.code = uvsr::SettingsSnapshotErrorCode::Path;
            error.message = "cannot convert snapshot catalog path";
            error.nativeCode = GetLastError();
        }
        const bool accepted = converted && catalogs.Append(path, error);
        free(path);
        return accepted;
    }
    int Fail(const uvsr::SettingsSnapshotError& error)
    {
        fprintf(stderr, "error: %s\n", error.Message());
        return EXIT_FAILURE;
    }
    bool Write(std::string_view text)
    {
        return fwrite(text.data(), 1, text.size(), stdout) == text.size();
    }
}

int main(int argumentCount, char** arguments)
{
    using namespace uvsr;
    if (argumentCount < 2)
    {
        PrintUsage();
        return EXIT_FAILURE;
    }
    const std::string_view code(arguments[1]);
    bool outputJson = false;
    SettingsSnapshotCatalogPaths catalogs;
    SettingsSnapshotError error;
    for (int index = 2; index < argumentCount; ++index)
    {
        const std::string_view argument(arguments[index]);
        if (argument == "--json") outputJson = true;
        else if (argument == "--catalog" && index + 1 < argumentCount)
        {
            if (!AppendPath(arguments[++index], catalogs, error)) return Fail(error);
        }
        else
        {
            PrintUsage();
            return EXIT_FAILURE;
        }
    }
#if defined(UVSR_BUILD_TESTING)
    constexpr auto location = SettingsSnapshotCatalogLocation::ExecutableState;
#else
    constexpr auto location = SettingsSnapshotCatalogLocation::Installed;
#endif
    if (!catalogs.Count() && !GetDefaultSettingsSnapshotCatalogPaths(code.substr(0, 4), location, catalogs, error))
        return Fail(error);
    DecodedSettings settings;
    if (!DecodeSettingsSnapshot(code, catalogs, settings, error)) return Fail(error);
    if (outputJson)
    {
        json::EncodedText output;
        if (!FormatDecodedSettingsJson(settings, output, error)) return Fail(error);
        if (!Write({output.Data(), output.Size()})) return EXIT_FAILURE;
    }
    else
    {
        for (size_t index = 0; index < settings.Count(); ++index)
        {
            const auto& setting = settings.Entries()[index];
            if (!Write(setting.name) || !Write("=") || !Write(setting.value) || !Write("\n")) return EXIT_FAILURE;
        }
    }
    return fflush(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
