#include "windows_path_text.h"
#include "scene_catalog_path.h"
#include <limits.h>
#include <stdlib.h>
#include <wchar.h>
#include <winerror.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_WINDOWS_PATH_TEXT_TEST_HOOKS)
        thread_local size_t AllocationsBeforeFailure = SIZE_MAX;
#endif
        void* Allocate(size_t bytes) noexcept
        {
#if defined(UVSR_WINDOWS_PATH_TEXT_TEST_HOOKS)
            if (AllocationsBeforeFailure != SIZE_MAX)
            {
                if (!AllocationsBeforeFailure) return nullptr;
                --AllocationsBeforeFailure;
            }
#endif
            return malloc(bytes);
        }
        bool Fail(WindowsPathTextResult& result, WindowsPathTextError error,
            uint32_t nativeCode = 0) noexcept
        {
            result = {error, nativeCode};
            return false;
        }
    }

    WindowsPathText::~WindowsPathText() noexcept { Clear(); }
    WindowsPathText::WindowsPathText(WindowsPathText&& other) noexcept
        : m_Data(other.m_Data), m_Size(other.m_Size)
    {
        other.m_Data = nullptr;
        other.m_Size = 0;
    }
    WindowsPathText& WindowsPathText::operator=(WindowsPathText&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            m_Data = other.m_Data;
            m_Size = other.m_Size;
            other.m_Data = nullptr;
            other.m_Size = 0;
        }
        return *this;
    }
    void WindowsPathText::Clear() noexcept
    {
        free(m_Data);
        m_Data = nullptr;
        m_Size = 0;
    }

    bool WindowsPathText::Assign(const wchar_t* path, size_t size, WindowsPathTextForm form,
        WindowsPathTextEncoding encoding, WindowsPathTextResult& result) noexcept
    {
        result = {};
        if (size > size_t(INT_MAX) || size >= size_t(PTRDIFF_MAX) / sizeof(wchar_t))
            return Fail(result, WindowsPathTextError::Capacity, ERROR_FILENAME_EXCED_RANGE);
        if ((!path && size) ||
            size > (UINTPTR_MAX - reinterpret_cast<uintptr_t>(path)) / sizeof(wchar_t) ||
            (form != WindowsPathTextForm::Native && form != WindowsPathTextForm::Generic &&
                form != WindowsPathTextForm::NormalizedGeneric) ||
            (encoding != WindowsPathTextEncoding::Filesystem && encoding != WindowsPathTextEncoding::Utf8))
            return Fail(result, WindowsPathTextError::InvalidPath);
        const wchar_t* input = path ? path : L"";
        wchar_t* scratch = nullptr;
        if (form != WindowsPathTextForm::Native)
        {
            scratch = static_cast<wchar_t*>(Allocate((size + 1) * sizeof(wchar_t)));
            if (!scratch) return Fail(result, WindowsPathTextError::Allocation);
            if (size) wmemcpy(scratch, input, size);
            scratch[size] = L'\0';
            if (form == WindowsPathTextForm::NormalizedGeneric)
                size = catalog_path::Normalize(scratch, size);
            // transform UTF-16 before encoding; a DBCS trail byte can be 0x5c.
            catalog_path::MakeGeneric(scratch, size);
            input = scratch;
        }
        const std::wstring_view view(input, size);
        catalog_path::ConversionPlan plan;
        catalog_path::Error error;
        if (!catalog_path::MeasureEncode(view, plan, error, encoding == WindowsPathTextEncoding::Utf8 ?
                catalog_path::Encoding::Utf8 : catalog_path::Encoding::Filesystem))
        {
            free(scratch);
            return Fail(result, WindowsPathTextError::Conversion, error.nativeCode);
        }
        char* candidate = static_cast<char*>(Allocate(plan.size + 1));
        if (!candidate)
        {
            free(scratch);
            return Fail(result, WindowsPathTextError::Allocation);
        }
        size_t written = 0;
        const bool encoded = catalog_path::Encode(view, plan, candidate, plan.size + 1, written, error);
        free(scratch);
        if (!encoded)
        {
            free(candidate);
            return Fail(result, WindowsPathTextError::Conversion, error.nativeCode);
        }
        Clear();
        m_Data = candidate;
        m_Size = written;
        return true;
    }
#if defined(UVSR_WINDOWS_PATH_TEXT_TEST_HOOKS)
    void FailWindowsPathTextAllocationAfter(size_t count) noexcept { AllocationsBeforeFailure = count; }
    void ClearWindowsPathTextAllocationFailure() noexcept { AllocationsBeforeFailure = SIZE_MAX; }
#endif
}
