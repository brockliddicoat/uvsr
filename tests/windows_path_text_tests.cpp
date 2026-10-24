#include "windows_path_text.h"
#include "windows_executable_path.h"
#include "color_lut_asset_path.h"
#include "settings_snapshot_storage.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool value, const char* message) noexcept
    {
        ++checks;
        if (!value) { ++failures; fprintf(stderr, "runtime path ownership: %s\n", message); }
    }
}

bool TestWindowsPathTextOwnership() noexcept
{
    using namespace uvsr;
    using Form = WindowsPathTextForm;
    using Encoding = WindowsPathTextEncoding;
    WindowsPathText text;
    WindowsPathTextResult error;
    constexpr wchar_t source[] = L"C:\\root\\a\\.\\b\\..\\c";
    constexpr const char* expected[] = {"C:\\root\\a\\.\\b\\..\\c", "C:/root/a/./b/../c", "C:/root/a/c"};
    constexpr Form forms[] = {Form::Native, Form::Generic, Form::NormalizedGeneric};
    for (size_t index = 0; index < 3; ++index)
    {
        Check(text.Assign(source, wcslen(source), forms[index], Encoding::Utf8, error) &&
            !strcmp(text.Data(), expected[index]), "spelling form selects the exact transformation");
        const char* prior = text.Data();
        const size_t priorSize = text.Size();
        for (size_t fail = 0; fail < (index ? 2u : 1u); ++fail)
        {
            FailWindowsPathTextAllocationAfter(fail);
            Check(!text.Assign(L"changed", 7, forms[index], Encoding::Utf8, error) &&
                error.error == WindowsPathTextError::Allocation && text.Data() == prior &&
                text.Size() == priorSize && !strcmp(text.Data(), expected[index]),
                "each allocation failure preserves pointer, size and bytes");
            ClearWindowsPathTextAllocationFailure();
        }
        FailWindowsPathTextAllocationAfter(0);
        Check(!text.Assign(reinterpret_cast<const wchar_t*>(uintptr_t(2)), size_t(INT_MAX) + 1,
                forms[index], Encoding::Utf8, error) && error.error == WindowsPathTextError::Capacity &&
                text.Data() == prior && text.Size() == priorSize,
            "capacity is checked before any allocation or input access");
        Check(!text.Assign(L"changed", 7, forms[index], Encoding::Utf8, error) &&
            error.error == WindowsPathTextError::Allocation && text.Data() == prior,
            "capacity rejection does not consume allocation hook");
        ClearWindowsPathTextAllocationFailure();
    }
    Check(!wcscmp(source, L"C:\\root\\a\\.\\b\\..\\c"), "normalization leaves borrowed input unchanged");
    Check(text.Assign(L"\u00e9", 1, Form::Native, Encoding::Utf8, error) && text.Size() == 2 &&
        !memcmp(text.Data(), "\xc3\xa9", 2), "explicit UTF-8 ignores filesystem code page");
    const char* retained = text.Data();
    const wchar_t surrogate[] = {wchar_t(0xd800)};
    Check(!text.Assign(surrogate, 1, Form::Native, Encoding::Utf8, error) &&
        error.error == WindowsPathTextError::Conversion && error.nativeCode == 1113 && text.Data() == retained,
        "invalid UTF-16 is a checked conversion failure");
    Check(!text.Assign(nullptr, 1, Form::Native, Encoding::Utf8, error) &&
        error.error == WindowsPathTextError::InvalidPath && text.Data() == retained, "null nonempty input is rejected");
    Check(!text.Assign(L"x", 1, Form(255), Encoding::Utf8, error) && text.Data() == retained,
        "invalid spelling form preserves text");
    Check(!text.Assign(L"x", 1, Form::Native, Encoding(255), error) && text.Data() == retained,
        "invalid encoding preserves text");
    FailWindowsPathTextAllocationAfter(0);
    WindowsPathText moved(static_cast<WindowsPathText&&>(text));
    Check(moved.Data() == retained && moved.Size() == 2 && text.Size() == 0 && !*text.Data(), "move transfers ownership without allocation");
    moved = static_cast<WindowsPathText&&>(moved);
    Check(moved.Data() == retained && moved.Size() == 2, "self-move retains text");
    text = static_cast<WindowsPathText&&>(moved);
    Check(text.Data() == retained && moved.Size() == 0 && !*moved.Data(), "move assignment transfers ownership");
    Check(!moved.Assign(L"x", 1, Form::Native, Encoding::Utf8, error) &&
        error.error == WindowsPathTextError::Allocation, "moves leave the allocation hook pending");
    ClearWindowsPathTextAllocationFailure();
    const wchar_t counted[] = {L'a', L'\0', L'b'};
    Check(text.Assign(counted, 3, Form::Native, Encoding::Utf8, error) && text.Size() == 3 &&
        !memcmp(text.Data(), "a\0b\0", 4), "counted NUL bytes and trailing terminator are preserved");
    Check(text.Assign(nullptr, 0, Form::NormalizedGeneric, Encoding::Utf8, error) && text.Size() == 0 &&
        !*text.Data(), "empty counted input succeeds");
    text.Clear(); text.Clear();
    Check(text.Size() == 0 && !*text.Data(), "clear is repeatable");

    ColorLutAssetPath lut;
    SettingsSnapshotError lutError;
    Check(lut.Prepare(L"C:\\media\\luts\\kodak", ToneMappingLut::Portra400, lutError) &&
        !strcmp(lut.DebugName(), "UVSR_Kodak_Portra_400"), "LUT path and fixed ASCII stem publish together");
    const wchar_t* lutPath = lut.Path().Data();
    FailWindowsPathOnce(WindowsPathError::Allocation);
    Check(!lut.Prepare(L"C:\\other", ToneMappingLut::Print2383, lutError) &&
        lutError.code == SettingsSnapshotErrorCode::OutOfMemory && lut.Path().Data() == lutPath &&
        !strcmp(lut.DebugName(), "UVSR_Kodak_Portra_400"), "failed LUT path allocation preserves path and debug stem");
    Check(!lut.Prepare(nullptr, ToneMappingLut::Print2383, lutError) && lut.Path().Data() == lutPath,
        "invalid LUT directory preserves path");
    Check(!lut.Prepare(L"C:\\other", ToneMappingLut::None, lutError) &&
        lutError.code == SettingsSnapshotErrorCode::InvalidInput && lut.Path().Data() == lutPath,
        "asset helper rejects None; the UI handles it before calling the loader");
    Check(!lut.Prepare(L"C:\\other", ToneMappingLut(255), lutError) && lut.Path().Data() == lutPath,
        "invalid LUT enum preserves the asset owner");
    printf("runtime path ownership checks: %u, failures: %u\n", checks, failures);
    return !failures;
}
