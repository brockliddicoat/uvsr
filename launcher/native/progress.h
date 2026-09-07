#pragma once
#include "core.h"
#include <algorithm>

namespace uvsr::launcher::ui
{
    inline std::wstring ProgressText(std::optional<int> value)
    {
        return value ? std::to_wstring(std::clamp(*value, 0, 100)) + L" percent" : L"In progress";
    }
    inline void DrawProgress(HDC dc, RECT bounds, std::optional<int> value, COLORREF background, COLORREF foreground)
    {
        HBRUSH track = CreateSolidBrush(background), fill = CreateSolidBrush(foreground);
        FillRect(dc, &bounds, track);
        RECT selected = bounds;
        const int width = bounds.right - bounds.left;
        if (value) selected.right = selected.left + width * std::clamp(*value, 0, 100) / 100;
        else { selected.left += width * 3 / 8; selected.right = bounds.left + width * 5 / 8; }
        FillRect(dc, &selected, fill);
        DeleteObject(track); DeleteObject(fill);
    }
}
