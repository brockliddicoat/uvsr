#include "renderer_import_path.h"

#include <string.h>

namespace uvsr
{
    namespace
    {
        bool Separator(char value) noexcept { return value == '/' || value == '\\'; }
        bool Drive(ArrayView<const char> path) noexcept
        {
            if (path.count < 2 || path.data[1] != ':') return false;
            const char letter = path.data[0];
            return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z');
        }

        size_t RootName(ArrayView<const char> path) noexcept
        {
            if (Drive(path)) return 2;
            if (path.count < 3 || !Separator(path.data[0])) return 0;
            const bool device = path.count >= 4 && Separator(path.data[3]) &&
                (path.count == 4 || !Separator(path.data[4])) &&
                ((Separator(path.data[1]) && (path.data[2] == '?' || path.data[2] == '.')) ||
                 (path.data[1] == '?' && path.data[2] == '?'));
            if (device) return 3;
            if (!Separator(path.data[1]) || Separator(path.data[2])) return 0;
            size_t end = 3;
            while (end < path.count && !Separator(path.data[end])) ++end;
            return end;
        }

        bool ValidRange(const void* data, size_t count) noexcept
        {
            return count <= size_t(PTRDIFF_MAX) && (!count || data) &&
                count <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(data);
        }

        bool ValidText(ArrayView<const char> text) noexcept
        {
            return ValidRange(text.data, text.count) &&
                (!text.count || !memchr(text.data, '\0', text.count));
        }

        struct PathParts
        {
            size_t base = 0;
            size_t first = 0;
            size_t length = 0;
            bool separator = false;
        };

        ImportResult Plan(ArrayView<const char> file, ArrayView<const char> reference, PathParts& parts) noexcept
        {
            if (!ValidText(file) || !ValidText(reference)) return {ImportError::InvalidInput};
            const size_t fileRoot = RootName(file), referenceRoot = RootName(reference);
            size_t relative = fileRoot;
            while (relative < file.count && Separator(file.data[relative])) ++relative;
            size_t parent = file.count;
            while (parent > relative && !Separator(file.data[parent - 1])) --parent;
            while (parent > relative && Separator(file.data[parent - 1])) --parent;

            const bool rooted = referenceRoot < reference.count && Separator(reference.data[referenceRoot]);
            const bool absolute = Drive(reference) ? rooted : referenceRoot != 0;
            const bool differentRoot = referenceRoot &&
                (referenceRoot != fileRoot || memcmp(file.data, reference.data, referenceRoot) != 0);
            if (absolute || differentRoot) parts.base = 0;
            else
            {
                parts.base = rooted ? fileRoot : parent;
                parts.first = referenceRoot;
                if (!rooted && parts.base)
                    parts.separator = parts.base == fileRoot ? fileRoot >= 3 : !Separator(file.data[parts.base - 1]);
            }
            const size_t tail = reference.count - parts.first;
            const size_t extra = parts.separator ? 1 : 0;
            if (tail > size_t(PTRDIFF_MAX) - extra || parts.base > size_t(PTRDIFF_MAX) - extra - tail)
                return {ImportError::Overflow};
            parts.length = parts.base + extra + tail;
            return {};
        }

        bool Overlaps(ArrayView<char> output, ArrayView<const char> input) noexcept
        {
            if (!output.count || !input.count) return false;
            const uintptr_t destination = reinterpret_cast<uintptr_t>(output.data);
            const uintptr_t source = reinterpret_cast<uintptr_t>(input.data);
            return destination < source + input.count && source < destination + output.count;
        }
    }

    ImportResult MeasureImportPath(ArrayView<const char> file, ArrayView<const char> reference, size_t& length) noexcept
    {
        PathParts parts;
        const auto result = Plan(file, reference, parts);
        if (result) length = parts.length;
        return result;
    }

    ImportResult ResolveImportPath(ArrayView<const char> file, ArrayView<const char> reference,
        ArrayView<char> output, size_t& length) noexcept
    {
        PathParts parts;
        const auto result = Plan(file, reference, parts);
        if (!result) return result;
        if (!ValidRange(output.data, output.count)) return {ImportError::InvalidOutput};
        if (output.count <= parts.length) return {ImportError::Capacity};
        if (Overlaps(output, file) || Overlaps(output, reference)) return {ImportError::InvalidOutput};
        size_t cursor = 0;
        for (size_t i = 0; i < parts.base; ++i)
            output.data[cursor++] = Separator(file.data[i]) ? '/' : file.data[i];
        if (parts.separator) output.data[cursor++] = '/';
        for (size_t i = parts.first; i < reference.count; ++i)
            output.data[cursor++] = Separator(reference.data[i]) ? '/' : reference.data[i];
        output.data[cursor] = '\0';
        length = cursor;
        return {};
    }

    ImportResult ReadImportPathFilename(ArrayView<const char> path, ArrayView<const char>& filename) noexcept
    {
        if (!ValidText(path)) return {ImportError::InvalidInput};
        size_t first = RootName(path);
        for (size_t i = first; i < path.count; ++i) if (Separator(path.data[i])) first = i + 1;
        filename = first < path.count ? ArrayView<const char>{path.data + first, path.count - first} : ArrayView<const char>{};
        return {};
    }

    ImportResult ReadImportPathExtension(ArrayView<const char> path, ArrayView<const char>& extension) noexcept
    {
        ArrayView<const char> name;
        const auto result = ReadImportPathFilename(path, name);
        if (!result) return result;
        const size_t filename = name.count ? size_t(name.data - path.data) : path.count;
        size_t end = filename;
        while (end < path.count && path.data[end] != ':') ++end;
        size_t dot = end;
        for (size_t i = filename; i < end; ++i) if (path.data[i] == '.') dot = i;
        if (dot == end || dot == filename ||
            (end - filename == 2 && path.data[filename] == '.' && path.data[filename + 1] == '.'))
            extension = {};
        else extension = {path.data + dot, end - dot};
        return {};
    }
}
