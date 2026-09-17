#include "uvsr_command_line.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <limits.h>
#include <type_traits>
#include <utility>

namespace
{
    size_t Assertions = 0;
    bool Passed = true;
    bool Require(bool value, const char* message)
    {
        ++Assertions;
        if (!value) { Passed = false; fprintf(stderr, "restart: %s\n", message); }
        return value;
    }
}

bool TestRestartCommandLineOwnership()
{
    using namespace uvsr;
    static_assert(!std::is_copy_constructible_v<RestartCommandLine>);
    static_assert(std::is_nothrow_move_constructible_v<RestartCommandLine>);
    static_assert(std::is_nothrow_move_assignable_v<RestartCommandLine>);
    Assertions = 0; Passed = true; ClearRestartCommandLineAllocationFailure();
    RestartCommandLine buffer; RestartCommandLineError error;
    Require(!buffer.Data() && !buffer.Size(), "empty owner exposed storage");
    wchar_t input[] = L"\"C:\\UVSR\\uvsr-engine.exe\" -adapter 2";
    if (!Require(buffer.Prepare(input, -1, error), "initial preparation failed")) return false;
    const auto* retained = buffer.Data(); const auto retainedSize = buffer.Size();
    Require(buffer.Data() != input && wcscmp(buffer.Data(), input) == 0 && buffer.Size() == wcslen(input),
        "initial text was not copied exactly");
    const auto unchanged = [&]() { return buffer.Data() == retained && buffer.Size() == retainedSize && wcscmp(buffer.Data(), input) == 0; };
    Require(!buffer.Prepare(nullptr, 0, error) && error == RestartCommandLineError::InvalidInput && unchanged(),
        "null input changed the published buffer");
    FailRestartCommandLineAllocationAfter(0);
    const bool allocated = buffer.Prepare(L"replacement", INT_MAX, error);
    ClearRestartCommandLineAllocationFailure();
    Require(!allocated && error == RestartCommandLineError::OutOfMemory && unchanged(),
        "allocation failure changed text or borrowed pointer");
    input[1] = L'Z';
    Require(buffer.Data()[1] == L'C', "prepared text still borrowed input storage");
    input[1] = L'C';
    if (!Require(buffer.Prepare(buffer.Data(), INT_MAX, error), "self-aliased preparation failed")) return false;
    Require(wcscmp(buffer.Data(), L"\"C:\\UVSR\\uvsr-engine.exe\" -adapter 2 -adapter 2147483647") == 0,
        "self alias or maximum adapter formatting changed text");
    if (!Require(buffer.Prepare(buffer.Data() + retainedSize, 0, error), "interior alias failed")) return false;
    Require(wcscmp(buffer.Data(), L" -adapter 2147483647 -adapter 0") == 0, "interior alias or zero adapter changed text");
    buffer.Data()[0] = L'x';
    Require(buffer.Data()[0] == L'x' && buffer.Data()[buffer.Size()] == L'\0', "buffer is not exclusively mutable and terminated");

    FailRestartCommandLineAllocationAfter(1);
    const bool oneAllocation = buffer.Prepare(L"one", 9, error);
    const auto* oneBorrow = buffer.Data();
    const bool secondAllocation = buffer.Prepare(L"two", 10, error);
    ClearRestartCommandLineAllocationFailure();
    Require(oneAllocation && !secondAllocation && error == RestartCommandLineError::OutOfMemory &&
        buffer.Data() == oneBorrow && wcscmp(buffer.Data(), L"one -adapter 9") == 0,
        "preparation did not use exactly one allocation or preserve a failed replacement");

    constexpr size_t longLength = 32768;
    auto* longInput = static_cast<wchar_t*>(malloc((longLength + 1) * sizeof(wchar_t)));
    if (!Require(longInput != nullptr, "long input fixture allocation failed")) return false;
    for (size_t index = 0; index < longLength; ++index) longInput[index] = L'x';
    longInput[longLength] = L'\0';
    const bool longPrepared = buffer.Prepare(longInput, INT_MAX, error);
    free(longInput);
    Require(longPrepared && buffer.Size() == longLength + 20 &&
        wcscmp(buffer.Data() + longLength, L" -adapter 2147483647") == 0,
        "text constructor introduced an OS policy cap or truncated the suffix");
    if (!longPrepared) return false;
    const auto* moveBorrow = buffer.Data(); const auto moveSize = buffer.Size();
    RestartCommandLine moved(std::move(buffer));
    Require(!buffer.Data() && !buffer.Size() && moved.Data() == moveBorrow && moved.Size() == moveSize,
        "move construction did not transfer exclusive storage");
    if (!Require(buffer.Prepare(L"old destination", -2, error), "move destination seed failed")) return false;
    buffer = std::move(moved);
    Require(!moved.Data() && !moved.Size() && buffer.Data() == moveBorrow && buffer.Size() == moveSize,
        "move assignment did not transfer storage and clear source");
    auto& self = buffer; buffer = std::move(self);
    Require(buffer.Data() == moveBorrow && buffer.Size() == moveSize, "self move lost storage");
    buffer.Clear(); buffer.Clear();
    Require(!buffer.Data() && !buffer.Size(), "clear retained storage");
    Require(buffer.Prepare(L"", INT_MIN, error) && buffer.Data() && !buffer.Size() && buffer.Data()[0] == L'\0',
        "empty command or negative adapter changed the original construction");
    const wchar_t raw[] = {wchar_t(0xd800), wchar_t(0xdfff), wchar_t(0xffff), L'\0', L'x', L'\0'};
    Require(buffer.Prepare(raw, -1, error) && buffer.Size() == 3 && wmemcmp(buffer.Data(), raw, 4) == 0,
        "raw UTF-16 or original terminator behavior changed");
    ClearRestartCommandLineAllocationFailure();
    printf("restart ownership: %zu assertions, allocation failure, aliases, one allocation, moves and mutable UTF-16 passed\n", Assertions);
    return Passed;
}

#if defined(UVSR_RESTART_COMMAND_LINE_STANDALONE_PROBE)
int main() { return TestRestartCommandLineOwnership() ? 0 : 1; }
#endif
