#include "scene_catalog_path.h"

#include <Windows.h>
#include <limits.h>
#include <locale.h>
#include <string.h>
#include <wchar.h>

namespace uvsr::catalog_path
{
    namespace
    {
        bool Separator(wchar_t value) noexcept { return value == L'/' || value == L'\\'; }
        bool Drive(std::wstring_view text) noexcept
        {
            return text.size() >= 2 && text[1] == L':' &&
                ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z'));
        }
        bool Fail(Error& error, ErrorCode code, DWORD nativeCode) noexcept
        {
            error = {code, nativeCode};
            return false;
        }
        bool Valid(const void* data, size_t count, size_t width, Error& error) noexcept
        {
            if (count > size_t(INT_MAX) || count > size_t(PTRDIFF_MAX) / width)
                return Fail(error, ErrorCode::Capacity, ERROR_FILENAME_EXCED_RANGE);
            if ((count && !data) || count * width > UINTPTR_MAX - reinterpret_cast<uintptr_t>(data))
                return Fail(error, ErrorCode::InvalidInput, ERROR_INVALID_PARAMETER);
            return true;
        }
        UINT CodePage() noexcept
        {
            if (___lc_codepage_func() == CP_UTF8) return CP_UTF8;
            return AreFileApisANSI() ? CP_ACP : CP_OEMCP;
        }
        int Narrow(std::wstring_view input, UINT page, char* output, int capacity, DWORD& code) noexcept
        {
            int size;
            if (page == CP_UTF8 || page == 54936)
                size = WideCharToMultiByte(page, WC_ERR_INVALID_CHARS, input.data(), int(input.size()), output, capacity, nullptr, nullptr);
            else
            {
                BOOL replaced = FALSE;
                size = WideCharToMultiByte(page, WC_NO_BEST_FIT_CHARS, input.data(), int(input.size()), output, capacity, nullptr, &replaced);
                if (replaced) { code = ERROR_NO_UNICODE_TRANSLATION; return 0; }
            }
            code = size ? ERROR_SUCCESS : GetLastError();
            if (code == ERROR_INVALID_FLAGS)
            {
                size = WideCharToMultiByte(page, 0, input.data(), int(input.size()), output, capacity, nullptr, nullptr);
                code = size ? ERROR_SUCCESS : GetLastError();
            }
            return size;
        }
    }

    bool MeasureDecode(std::string_view input, Encoding encoding, ConversionPlan& plan, Error& error) noexcept
    {
        error = {};
        if (!Valid(input.data(), input.size(), 1, error)) return false;
        const UINT page = encoding == Encoding::Utf8 ? CP_UTF8 : CodePage();
        if (input.empty()) { plan = {0, page}; return true; }
        const int count = MultiByteToWideChar(page,
            MB_ERR_INVALID_CHARS, input.data(), int(input.size()), nullptr, 0);
        if (!count) return Fail(error, ErrorCode::Conversion, GetLastError());
        plan = {size_t(count), page};
        return true;
    }
    bool Decode(std::string_view input, const ConversionPlan& plan, wchar_t* output,
        size_t capacity, size_t& size, Error& error) noexcept
    {
        error = {};
        const size_t count = plan.size;
        if (!Valid(input.data(), input.size(), 1, error) || !Valid(output, count, sizeof(wchar_t), error)) return false;
        if (capacity <= count) return Fail(error, ErrorCode::Capacity, ERROR_INSUFFICIENT_BUFFER);
        if (!output) return Fail(error, ErrorCode::InvalidInput, ERROR_INVALID_PARAMETER);
        if (count && !MultiByteToWideChar(plan.codePage,
            MB_ERR_INVALID_CHARS, input.data(), int(input.size()), output, int(count)))
            return Fail(error, ErrorCode::Conversion, GetLastError());
        output[count] = L'\0'; size = count;
        return true;
    }
    bool MeasureEncode(std::wstring_view input, ConversionPlan& plan, Error& error, Encoding encoding) noexcept
    {
        error = {};
        if (!Valid(input.data(), input.size(), sizeof(wchar_t), error)) return false;
        const UINT page = encoding == Encoding::Utf8 ? CP_UTF8 : CodePage();
        if (input.empty()) { plan = {0, page}; return true; }
        DWORD code;
        const int count = Narrow(input, page, nullptr, 0, code);
        if (!count) return Fail(error, ErrorCode::Conversion, code);
        plan = {size_t(count), page};
        return true;
    }
    bool Encode(std::wstring_view input, const ConversionPlan& plan, char* output, size_t capacity, size_t& size, Error& error) noexcept
    {
        error = {};
        const size_t count = plan.size;
        if (!Valid(input.data(), input.size(), sizeof(wchar_t), error) || !Valid(output, count, 1, error)) return false;
        if (capacity <= count) return Fail(error, ErrorCode::Capacity, ERROR_INSUFFICIENT_BUFFER);
        if (!output) return Fail(error, ErrorCode::InvalidInput, ERROR_INVALID_PARAMETER);
        DWORD code;
        if (count && !Narrow(input, plan.codePage, output, int(count), code)) return Fail(error, ErrorCode::Conversion, code);
        output[count] = '\0'; size = count;
        return true;
    }

    size_t RootName(std::wstring_view text) noexcept
    {
        if (Drive(text)) return 2;
        if (text.size() < 3 || !Separator(text[0])) return 0;
        if (text.size() >= 4 && Separator(text[3]) && (text.size() == 4 || !Separator(text[4])) &&
            ((Separator(text[1]) && (text[2] == L'?' || text[2] == L'.')) || (text[1] == L'?' && text[2] == L'?')))
            return 3;
        if (!Separator(text[1]) || Separator(text[2])) return 0;
        size_t end = 3;
        while (end < text.size() && !Separator(text[end])) ++end;
        return end;
    }
    size_t RelativeStart(std::wstring_view text) noexcept
    {
        size_t first = RootName(text);
        while (first < text.size() && Separator(text[first])) ++first;
        return first;
    }
    size_t FilenameStart(std::wstring_view text) noexcept
    {
        const size_t first = RelativeStart(text);
        size_t end = text.size();
        while (end > first && !Separator(text[end - 1])) --end;
        return end;
    }
    size_t ParentLength(std::wstring_view text) noexcept
    {
        const size_t first = RelativeStart(text);
        size_t end = text.size();
        while (end > first && !Separator(text[end - 1])) --end;
        while (end > first && Separator(text[end - 1])) --end;
        return end;
    }
    size_t Normalize(wchar_t* text, size_t size) noexcept
    {
        if (!size) return 0;
        const size_t root = RootName({text, size});
        for (size_t i = 0; i < root; ++i) if (Separator(text[i])) text[i] = L'\\';
        size_t read = root, write = root;
        const bool rooted = read < size && Separator(text[read]);
        if (rooted)
        {
            while (read < size && Separator(text[read])) ++read;
            text[write++] = L'\\';
        }
        const size_t first = write;
        while (read < size)
        {
            const size_t begin = read;
            while (read < size && !Separator(text[read])) ++read;
            const size_t length = read - begin;
            const bool trailing = read < size;
            while (read < size && Separator(text[read])) ++read;
            if (length == 1 && text[begin] == L'.') continue;
            const bool parent = length == 2 && text[begin] == L'.' && text[begin + 1] == L'.';
            if (parent)
            {
                size_t previousEnd = write;
                if (previousEnd > first && Separator(text[previousEnd - 1])) --previousEnd;
                size_t previous = previousEnd;
                while (previous > first && !Separator(text[previous - 1])) --previous;
                if (previousEnd > first && !(previousEnd - previous == 2 && text[previous] == L'.' && text[previous + 1] == L'.'))
                {
                    write = previous;
                    continue;
                }
                if (rooted) continue;
            }
            wmemmove(text + write, text + begin, length);
            write += length;
            if (trailing) text[write++] = L'\\';
        }
        if (write >= first + 3 && text[write - 1] == L'\\' && text[write - 2] == L'.' && text[write - 3] == L'.' &&
            (write == first + 3 || text[write - 4] == L'\\')) --write;
        if (!write) text[write++] = L'.';
        text[write] = L'\0';
        return write;
    }
    void MakeGeneric(wchar_t* text, size_t size) noexcept
    {
        for (size_t i = 0; i < size; ++i) if (text[i] == L'\\') text[i] = L'/';
    }
    bool PlanJoin(std::wstring_view left, std::wstring_view right, JoinPlan& plan, Error& error) noexcept
    {
        error = {};
        if (!Valid(left.data(), left.size(), sizeof(wchar_t), error) ||
            !Valid(right.data(), right.size(), sizeof(wchar_t), error)) return false;
        const size_t root = RootName(left), otherRoot = RootName(right);
        const bool rooted = otherRoot < right.size() && Separator(right[otherRoot]);
        const bool absolute = Drive(right) ? rooted : otherRoot != 0;
        const bool different = otherRoot && (root != otherRoot || wmemcmp(left.data(), right.data(), root) != 0);
        JoinPlan candidate;
        if (!absolute && !different)
        {
            candidate.base = rooted ? root : left.size();
            candidate.first = otherRoot;
            if (!rooted && !left.empty())
                candidate.separator = root == left.size() ? root >= 3 : !Separator(left.back());
        }
        const size_t tail = right.size() - candidate.first, extra = candidate.separator ? 1 : 0;
        if (tail > size_t(INT_MAX) - extra || candidate.base > size_t(INT_MAX) - extra - tail)
            return Fail(error, ErrorCode::Capacity, ERROR_FILENAME_EXCED_RANGE);
        candidate.size = candidate.base + extra + tail;
        plan = candidate;
        return true;
    }
    bool Join(std::wstring_view left, std::wstring_view right, wchar_t* output,
        size_t capacity, size_t& size, Error& error) noexcept
    {
        JoinPlan plan;
        if (!PlanJoin(left, right, plan, error)) return false;
        if (capacity <= plan.size) return Fail(error, ErrorCode::Capacity, ERROR_INSUFFICIENT_BUFFER);
        if (!output) return Fail(error, ErrorCode::InvalidInput, ERROR_INVALID_PARAMETER);
        if (plan.base) wmemmove(output, left.data(), plan.base);
        size_t cursor = plan.base;
        if (plan.separator) output[cursor++] = L'\\';
        if (right.size() > plan.first) wmemmove(output + cursor, right.data() + plan.first, right.size() - plan.first);
        output[plan.size] = L'\0'; size = plan.size;
        return true;
    }
    bool Next(std::wstring_view path, size_t& cursor, std::wstring_view& component) noexcept
    {
        if (cursor > path.size() || path.empty()) return false;
        const size_t root = RootName(path), relative = RelativeStart(path);
        size_t first = cursor, end = first;
        if (first == 0 && root) end = root;
        else if (first == root && relative > root) end = relative;
        else
        {
            while (first < path.size() && Separator(path[first])) ++first;
            end = first;
            while (end < path.size() && !Separator(path[end])) ++end;
        }
        component = path.substr(first, end - first);
        cursor = end;
        if (end == path.size()) cursor = path.size() + 1;
        return true;
    }
}
