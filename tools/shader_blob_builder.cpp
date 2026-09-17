#include <Windows.h>

#include <ctype.h>
#include <errno.h>
#include <filesystem>
#include <limits.h>
#include <new>
#include <process.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace fs = std::filesystem;

namespace
{
    size_t allocationAttempt = 0;
    size_t failedAllocationAttempt = 0;
    HANDLE temporaryHandle = INVALID_HANDLE_VALUE;
    const wchar_t* temporaryPath = nullptr;

    void* allocate(size_t size) noexcept
    {
        ++allocationAttempt;
        if (failedAllocationAttempt && allocationAttempt == failedAllocationAttempt)
            return nullptr;
        return malloc(size ? size : 1);
    }

    // keep Windows path normalization and Unicode rules in the standard library.
    // only this standalone tool owns that fatal allocation boundary. the active
    // temporary is closed and removed before exit; the published file is intact.
    [[noreturn]] void path_allocation_failed() noexcept
    {
        constexpr char message[] = "shader blob builder: out of memory in path storage\n";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message) - 1, &written, nullptr);
        if (temporaryHandle != INVALID_HANDLE_VALUE) CloseHandle(temporaryHandle);
        if (temporaryPath) DeleteFileW(temporaryPath);
        _exit(1);
    }
}

void* operator new(size_t size)
{
    if (void* value = allocate(size)) return value;
    path_allocation_failed();
}
void* operator new[](size_t size) { return ::operator new(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void operator delete(void* value) noexcept { free(value); }
void operator delete[](void* value) noexcept { free(value); }
void operator delete(void* value, size_t) noexcept { free(value); }
void operator delete[](void* value, size_t) noexcept { free(value); }
void operator delete(void* value, const std::nothrow_t&) noexcept { free(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { free(value); }

namespace
{
    bool error(const char* reason, const char* path = nullptr, DWORD systemCode = 0) noexcept
    {
        fprintf(stderr, "shader blob builder: %s", reason);
        if (path) fprintf(stderr, " %s", path);
        if (systemCode) fprintf(stderr, " (Win32 error %lu)", systemCode);
        fputc('\n', stderr);
        return false;
    }

    bool configure_allocation_failure() noexcept
    {
#if defined(UVSR_BUILD_TESTING)
        char text[32]{};
        const DWORD size = GetEnvironmentVariableA(
            "UVSR_SHADER_TOOL_FAIL_ALLOCATION", text, DWORD(sizeof(text)));
        if (size)
        {
            if (size >= sizeof(text)) return error("invalid allocation failure fixture");
            char* end = nullptr;
            errno = 0;
            const unsigned long long value = strtoull(text, &end, 10);
            if (errno || end == text || *end || value > SIZE_MAX || text[0] == '-')
                return error("invalid allocation failure fixture");
            failedAllocationAttempt = size_t(value);
        }
#endif
        allocationAttempt = 0;
        return true;
    }

    size_t next_capacity(size_t capacity, size_t required, size_t stride) noexcept
    {
        if (required <= capacity) return capacity;
        const size_t limit = size_t(PTRDIFF_MAX) / stride;
        if (required > limit)
        {
            error("collection capacity exceeded");
            return 0;
        }
        size_t next = capacity ? capacity : (limit < 16 ? limit : 16);
        while (next < required) next = next > limit / 2 ? limit : next * 2;
        return next;
    }

    struct Bytes
    {
        char* data = nullptr;
        size_t size = 0;
        ~Bytes() noexcept { free(data); }
        Bytes() = default;
        Bytes(const Bytes&) = delete;
        Bytes& operator=(const Bytes&) = delete;

        bool prepare(size_t count) noexcept
        {
            if (count >= size_t(PTRDIFF_MAX)) return error("file capacity exceeded");
            char* candidate = static_cast<char*>(allocate(count + 1));
            if (!candidate) return error("out of memory");
            candidate[count] = '\0';
            free(data);
            data = candidate;
            size = count;
            return true;
        }
    };

    bool path_text(const fs::path& path, Bytes& text, bool generic)
    {
        const auto& native = path.native();
        if (native.size() > INT_MAX) return error("path capacity exceeded");
        const int size = native.empty() ? 0 : WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            native.data(), int(native.size()), nullptr, 0, nullptr, nullptr);
        if (!size && !native.empty()) return error("filesystem path cannot be encoded as UTF-8");
        if (!text.prepare(size_t(size))) return false;
        if (size && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native.data(),
            int(native.size()), text.data, size, nullptr, nullptr) != size)
            return error("filesystem path cannot be encoded as UTF-8");
        if (generic)
            for (size_t index = 0; index < text.size; ++index)
                if (text.data[index] == '\\') text.data[index] = '/';
        return true;
    }

    bool path_error(const char* reason, const fs::path& path, DWORD systemCode = 0)
    {
        Bytes text;
        return path_text(path, text, false) ? error(reason, text.data, systemCode) :
            error(reason, nullptr, systemCode);
    }

    bool make_path(const char* text, size_t size, fs::path& path)
    {
        if (size > INT_MAX) return error("path capacity exceeded");
        if (!size)
        {
            path.clear();
            return true;
        }
        if (memchr(text, '\0', size) ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, int(size), nullptr, 0))
            return error("invalid UTF-8 path");
        path = fs::u8path(text, text + size);
        return true;
    }

    fs::path normalized_existing_path(const fs::path& path)
    {
        std::error_code failure;
        fs::path normalized = fs::weakly_canonical(path, failure);
        if (failure)
        {
            failure.clear();
            normalized = fs::absolute(path, failure).lexically_normal();
            if (failure) normalized = path.lexically_normal();
        }
        return normalized;
    }

    struct File
    {
        HANDLE handle = INVALID_HANDLE_VALUE;
        const char* name = nullptr;
        ~File() noexcept { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        File() = default;
        File(const File&) = delete;
        File& operator=(const File&) = delete;

        bool open(const char* path)
        {
            fs::path native;
            if (!make_path(path, strlen(path), native)) return false;
            name = path;
            handle = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            return handle != INVALID_HANDLE_VALUE || error("cannot read", name, GetLastError());
        }
        bool length(uint64_t& size) const noexcept
        {
            LARGE_INTEGER count{};
            if (!GetFileSizeEx(handle, &count) || count.QuadPart < 0)
                return error("cannot measure file", name, GetLastError());
            size = uint64_t(count.QuadPart);
            return true;
        }
        bool read(void* bytes, size_t size) const noexcept
        {
            auto* next = static_cast<char*>(bytes);
            while (size)
            {
                const DWORD wanted = size > 1024 * 1024 ? 1024 * 1024 : DWORD(size);
                DWORD received = 0;
                if (!ReadFile(handle, next, wanted, &received, nullptr))
                    return error("cannot read complete file", name, GetLastError());
                if (received != wanted) return error("cannot read complete file", name);
                next += received;
                size -= received;
            }
            return true;
        }
        bool close() noexcept
        {
            const HANDLE current = handle;
            handle = INVALID_HANDLE_VALUE;
            return CloseHandle(current) || error("cannot close input file", name, GetLastError());
        }
    };

    bool read_text(const char* path, Bytes& text)
    {
        File file;
        uint64_t size = 0;
        if (!file.open(path) || !file.length(size)) return false;
        if (size >= uint64_t(PTRDIFF_MAX)) return error("file capacity exceeded", path);
        return text.prepare(size_t(size)) && file.read(text.data, text.size) && file.close();
    }

    struct Indices
    {
        size_t* values = nullptr;
        size_t count = 0;
        size_t capacity = 0;
        ~Indices() noexcept { delete[] values; }
        bool push(size_t value) noexcept
        {
            if (count == SIZE_MAX) return error("worklist capacity exceeded");
            if (count == capacity)
            {
                const size_t next = next_capacity(capacity, count + 1, sizeof(size_t));
                if (!next) return false;
                auto* candidate = new (std::nothrow) size_t[next];
                if (!candidate) return error("out of memory");
                if (count) memcpy(candidate, values, count * sizeof(size_t));
                delete[] values;
                values = candidate;
                capacity = next;
            }
            values[count++] = value;
            return true;
        }
    };

    struct Dependencies
    {
        struct Entry { char* path; bool scanned; };
        Entry* entries = nullptr;
        size_t count = 0;
        size_t capacity = 0;
        ~Dependencies() noexcept
        {
            for (size_t index = 0; index < count; ++index) free(entries[index].path);
            delete[] entries;
        }
        bool insert(const fs::path& path, size_t& index)
        {
            Bytes key;
            if (!path_text(path, key, true)) return false;
            for (index = 0; index < count; ++index)
                if (strcmp(entries[index].path, key.data) == 0) return true;
            if (count == SIZE_MAX)
                return error("dependency capacity exceeded");
            if (count == capacity)
            {
                const size_t next = next_capacity(capacity, count + 1, sizeof(Entry));
                if (!next) return false;
                auto* candidate = new (std::nothrow) Entry[next];
                if (!candidate) return error("out of memory");
                for (size_t old = 0; old < count; ++old) candidate[old] = entries[old];
                delete[] entries;
                entries = candidate;
                capacity = next;
            }
            entries[count++] = {key.data, false};
            key.data = nullptr;
            return true;
        }
        void sift(size_t root, size_t size) noexcept
        {
            while (root < size / 2)
            {
                size_t child = root * 2 + 1;
                if (child + 1 < size && strcmp(entries[child].path, entries[child + 1].path) < 0)
                    ++child;
                if (strcmp(entries[root].path, entries[child].path) >= 0) return;
                const Entry saved = entries[root]; entries[root] = entries[child]; entries[child] = saved;
                root = child;
            }
        }
        void sort() noexcept
        {
            for (size_t index = count / 2; index; --index) sift(index - 1, count);
            for (size_t size = count; size > 1;)
            {
                --size;
                const Entry saved = entries[0]; entries[0] = entries[size]; entries[size] = saved;
                sift(0, size);
            }
        }
    };

    void prepare_source(Bytes& source) noexcept
    {
        size_t size = 0;
        for (size_t index = 0; index < source.size; ++index)
        {
            if (source.data[index] == '\\' && index + 1 < source.size && source.data[index + 1] == '\n')
            {
                source.data[size++] = ' ';
                ++index;
            }
            else if (source.data[index] == '\\' && index + 2 < source.size &&
                source.data[index + 1] == '\r' && source.data[index + 2] == '\n')
            {
                source.data[size++] = ' ';
                index += 2;
            }
            else source.data[size++] = source.data[index];
        }
        source.size = size;
        source.data[size] = '\0';
        enum class State { Code, LineComment, BlockComment, String, Character };
        State state = State::Code;
        for (size_t index = 0; index < size; ++index)
        {
            const char value = source.data[index];
            const char next = index + 1 < size ? source.data[index + 1] : '\0';
            if (state == State::LineComment)
            {
                if (value == '\n') state = State::Code;
                else source.data[index] = ' ';
            }
            else if (state == State::BlockComment)
            {
                if (value == '*' && next == '/')
                {
                    source.data[index] = source.data[index + 1] = ' ';
                    ++index;
                    state = State::Code;
                }
                else if (value != '\n') source.data[index] = ' ';
            }
            else if (state == State::String || state == State::Character)
            {
                if (value == '\\' && next != '\0') ++index;
                else if ((state == State::String && value == '"') ||
                    (state == State::Character && value == '\'')) state = State::Code;
            }
            else if (value == '/' && (next == '/' || next == '*'))
            {
                source.data[index] = source.data[index + 1] = ' ';
                ++index;
                state = next == '/' ? State::LineComment : State::BlockComment;
            }
            else if (value == '"') state = State::String;
            else if (value == '\'') state = State::Character;
        }
    }

    struct Directories
    {
        fs::path* paths = nullptr;
        size_t count = 0;
        ~Directories() noexcept { delete[] paths; }
        bool prepare(size_t maximum) noexcept
        {
            if (maximum > size_t(PTRDIFF_MAX) / sizeof(fs::path))
                return error("include-directory capacity exceeded");
            paths = new (std::nothrow) fs::path[maximum];
            return paths != nullptr || error("out of memory");
        }
    };

    bool existing_include(const fs::path& candidate, fs::path& result)
    {
        std::error_code failure;
        if (!fs::is_regular_file(candidate, failure) || failure) return false;
        result = normalized_existing_path(candidate);
        return true;
    }

    bool resolve_include(const fs::path& requested, bool quoted, const fs::path& source,
        const Directories& directories, fs::path& result)
    {
        if (requested.is_absolute()) return existing_include(requested, result);
        if (quoted && existing_include(source.parent_path() / requested, result)) return true;
        for (size_t index = 0; index < directories.count; ++index)
            if (existing_include(directories.paths[index] / requested, result)) return true;
        return false;
    }

    bool collect_includes(const fs::path& source, const Bytes& text,
        const Directories& directories, Dependencies& dependencies, Indices& children)
    {
        for (size_t line = 0; line < text.size;)
        {
            size_t end = line;
            while (end < text.size && text.data[end] != '\n') ++end;
            size_t position = line;
            line = end + 1;
            if (end - position >= 3 && static_cast<unsigned char>(text.data[position]) == 0xef &&
                static_cast<unsigned char>(text.data[position + 1]) == 0xbb &&
                static_cast<unsigned char>(text.data[position + 2]) == 0xbf) position += 3;
            while (position < end && isspace(static_cast<unsigned char>(text.data[position]))) ++position;
            if (position == end || text.data[position] != '#') continue;
            ++position;
            while (position < end && isspace(static_cast<unsigned char>(text.data[position]))) ++position;
            constexpr size_t keywordSize = sizeof("include") - 1;
            if (end - position < keywordSize || memcmp(text.data + position, "include", keywordSize)) continue;
            position += keywordSize;
            if (position < end && (isalnum(static_cast<unsigned char>(text.data[position])) ||
                text.data[position] == '_')) continue;
            while (position < end && isspace(static_cast<unsigned char>(text.data[position]))) ++position;
            if (position == end || (text.data[position] != '"' && text.data[position] != '<'))
                return path_error("non-literal shader include in", source);
            const bool quoted = text.data[position++] == '"';
            const char closing = quoted ? '"' : '>';
            size_t finish = position;
            while (finish < end && text.data[finish] != closing) ++finish;
            if (finish == end || finish == position) return path_error("malformed shader include in", source);
            fs::path requested;
            fs::path resolved;
            if (!make_path(text.data + position, finish - position, requested)) return false;
            // shared C++/HLSL files contain inactive native includes. DXC remains
            // authoritative for unresolved active directives and their errors.
            if (resolve_include(requested, quoted, source, directories, resolved))
            {
                size_t index = 0;
                if (!dependencies.insert(resolved, index) || !children.push(index)) return false;
            }
        }
        return true;
    }

    bool scan_shader_files(const fs::path& source, const Directories& directories, Dependencies& dependencies)
    {
        Indices pending;
        size_t index = 0;
        if (!dependencies.insert(normalized_existing_path(source), index) || !pending.push(index)) return false;
        while (pending.count)
        {
            index = pending.values[--pending.count];
            if (dependencies.entries[index].scanned) continue;
            dependencies.entries[index].scanned = true;
            const char* name = dependencies.entries[index].path;
            Bytes text;
            fs::path path;
            if (!make_path(name, strlen(name), path) || !read_text(name, text)) return false;
            prepare_source(text);
            Indices children;
            if (!collect_includes(path, text, directories, dependencies, children)) return false;
            for (size_t child = children.count; child; --child)
                if (!pending.push(children.values[child - 1])) return false;
        }
        dependencies.sort();
        return true;
    }

    struct Catalog
    {
        struct Entry { const char* key; size_t keySize; const char* object; uint64_t size; };
        Bytes text;
        Entry* entries = nullptr;
        size_t count = 0;
        uint64_t blobSize = 0;
        ~Catalog() noexcept { delete[] entries; }

        bool read(const char* path)
        {
            if (!read_text(path, text)) return false;
            size_t maximum = 1;
            for (size_t index = 0; index < text.size; ++index)
                if (text.data[index] == '\n') ++maximum;
            if (maximum > size_t(PTRDIFF_MAX) / sizeof(Entry)) return error("catalog capacity exceeded");
            entries = new (std::nothrow) Entry[maximum];
            if (!entries) return error("out of memory");
            for (size_t line = 0; line < text.size;)
            {
                size_t end = line;
                while (end < text.size && text.data[end] != '\n') ++end;
                const size_t begin = line;
                line = end + 1;
                if (end > begin && text.data[end - 1] == '\r') --end;
                if (end == begin) continue;
                const char* tab = static_cast<const char*>(memchr(text.data + begin, '\t', end - begin));
                if (!tab || memchr(tab + 1, '\t', size_t(text.data + end - tab - 1)))
                    return error("malformed shader family catalog row");
                const size_t keySize = size_t(tab - text.data - begin);
                if (tab + 1 == text.data + end) return error("invalid or duplicate shader permutation key");
                for (size_t previous = 0; previous < count; ++previous)
                    if (entries[previous].keySize == keySize &&
                        memcmp(entries[previous].key, text.data + begin, keySize) == 0)
                        return error("invalid or duplicate shader permutation key");
                const size_t pathSize = size_t(text.data + end - tab - 1);
                if (memchr(tab + 1, '\0', pathSize)) return error("invalid shader object path");
                text.data[end] = '\0';
                entries[count++] = {text.data + begin, keySize, tab + 1, 0};
            }
            if (!count) return error("shader family catalog is empty");
            blobSize = count == 1 && entries[0].keySize == 0 ? 0 : 4;
            for (size_t index = 0; index < count; ++index)
            {
                Entry& entry = entries[index];
                if (count != 1 && !entry.keySize) return error("a multi-permutation shader family has an empty key");
                File file;
                if (!file.open(entry.object) || !file.length(entry.size) || !file.close()) return false;
                if (!entry.size || entry.size > (uint64_t(1) << 30))
                    return error("shader object has an invalid size:", entry.object);
                if (entry.keySize > UINT32_MAX) return error("shader key capacity exceeded");
                const uint64_t bytes = entry.size + (blobSize ? uint64_t(entry.keySize) + 8 : 0);
                if (bytes > UINT64_MAX - blobSize) return error("shader family capacity exceeded");
                blobSize += bytes;
            }
            return true;
        }
        bool raw() const noexcept { return count == 1 && !entries[0].keySize; }
    };

    enum class Comparison { Different, Equal, Failed };

    Comparison compare_files(const fs::path& temporary, const fs::path& destination)
    {
        File candidate;
        File current;
        candidate.handle = CreateFileW(temporary.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (candidate.handle == INVALID_HANDLE_VALUE)
        {
            path_error("cannot verify temporary file", temporary, GetLastError());
            return Comparison::Failed;
        }
        current.handle = CreateFileW(destination.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (current.handle == INVALID_HANDLE_VALUE)
        {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return Comparison::Different;
            path_error("cannot compare published file", destination, code);
            return Comparison::Failed;
        }
        LARGE_INTEGER candidateSize{};
        LARGE_INTEGER currentSize{};
        if (!GetFileSizeEx(candidate.handle, &candidateSize) || !GetFileSizeEx(current.handle, &currentSize) ||
            candidateSize.QuadPart < 0 || currentSize.QuadPart < 0)
        {
            path_error("cannot measure published file comparison", destination, GetLastError());
            return Comparison::Failed;
        }
        if (candidateSize.QuadPart != currentSize.QuadPart) return Comparison::Different;
        char candidateBytes[64 * 1024];
        char currentBytes[64 * 1024];
        uint64_t remaining = uint64_t(candidateSize.QuadPart);
        while (remaining)
        {
            const DWORD wanted = remaining > sizeof(candidateBytes) ? DWORD(sizeof(candidateBytes)) : DWORD(remaining);
            DWORD receivedCandidate = 0;
            DWORD receivedCurrent = 0;
            if (!ReadFile(candidate.handle, candidateBytes, wanted, &receivedCandidate, nullptr) ||
                !ReadFile(current.handle, currentBytes, wanted, &receivedCurrent, nullptr) ||
                receivedCandidate != wanted || receivedCurrent != wanted)
            {
                path_error("cannot read complete file comparison", destination, GetLastError());
                return Comparison::Failed;
            }
            if (memcmp(candidateBytes, currentBytes, wanted)) return Comparison::Different;
            remaining -= wanted;
        }
        return Comparison::Equal;
    }

    struct Output
    {
        fs::path path;
        HANDLE handle = INVALID_HANDLE_VALUE;
        bool ownsTemporary = false;
        char buffer[64 * 1024];
        size_t buffered = 0;
        ~Output() noexcept
        {
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            if (ownsTemporary) DeleteFileW(path.c_str());
            temporaryHandle = INVALID_HANDLE_VALUE;
            temporaryPath = nullptr;
        }
        Output() = default;
        Output(const Output&) = delete;
        Output& operator=(const Output&) = delete;

        bool begin(const fs::path& destination)
        {
            const fs::path parent = destination.parent_path();
            std::error_code failure;
            if (!parent.empty()) fs::create_directories(parent, failure);
            if (failure) return path_error("cannot create output directory", parent, DWORD(failure.value()));
            wchar_t suffix[32]{};
            const int count = swprintf(suffix, sizeof(suffix) / sizeof(*suffix), L".tmp-%lu", GetCurrentProcessId());
            if (count <= 0 || size_t(count) >= sizeof(suffix) / sizeof(*suffix))
                return error("temporary path capacity exceeded");
            path = destination;
            path += suffix;
            handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return path_error("cannot create", path, GetLastError());
            ownsTemporary = true;
            temporaryHandle = handle;
            temporaryPath = path.c_str();
            return true;
        }
        bool flush()
        {
            size_t offset = 0;
            while (offset < buffered)
            {
                DWORD written = 0;
                if (!WriteFile(handle, buffer + offset, DWORD(buffered - offset), &written, nullptr) || !written)
                    return path_error("cannot write complete temporary file", path, GetLastError());
                offset += written;
            }
            buffered = 0;
            return true;
        }
        bool write(const void* bytes, size_t size)
        {
            const auto* next = static_cast<const char*>(bytes);
            while (size)
            {
                const size_t available = sizeof(buffer) - buffered;
                const size_t count = size < available ? size : available;
                memcpy(buffer + buffered, next, count);
                buffered += count;
                next += count;
                size -= count;
                if (buffered == sizeof(buffer) && !flush()) return false;
            }
            return true;
        }
        bool text(const char* value) { return write(value, strlen(value)); }
        bool publish(const fs::path& destination)
        {
            if (!flush()) return false;
            if (!FlushFileBuffers(handle)) return path_error("cannot finish", path, GetLastError());
            const HANDLE finished = handle;
            handle = INVALID_HANDLE_VALUE;
            temporaryHandle = INVALID_HANDLE_VALUE;
            if (!CloseHandle(finished)) return path_error("cannot close temporary file", path, GetLastError());
            const Comparison comparison = compare_files(path, destination);
            if (comparison == Comparison::Failed) return false;
            if (comparison == Comparison::Equal)
            {
                if (!DeleteFileW(path.c_str())) return path_error("cannot discard unchanged temporary file", path, GetLastError());
            }
            else if (!MoveFileExW(path.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return path_error("cannot publish", destination, GetLastError());
            ownsTemporary = false;
            temporaryPath = nullptr;
            return true;
        }
    };

    bool is_identifier(const char* value) noexcept
    {
        if (!*value || !(isalpha(static_cast<unsigned char>(*value)) || *value == '_')) return false;
        while (*++value)
            if (!(isalnum(static_cast<unsigned char>(*value)) || *value == '_')) return false;
        return true;
    }

    struct BlobOutput
    {
        Output& output;
        bool header;
        uint64_t total;
        uint64_t position = 0;

        bool start(const char* symbol)
        {
            return !header || (output.text("// Generated by uvsr_shader_blob_builder.\nconst uint8_t ") &&
                output.text(symbol) && output.text("[] = {\n"));
        }
        bool write(const void* data, size_t size)
        {
            if (position > total || uint64_t(size) > total - position)
                return error("shader family size changed while writing");
            if (!header)
            {
                position += size;
                return output.write(data, size);
            }
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (size_t index = 0; index < size; ++index)
            {
                if (position % 20 == 0 && !output.text("    ")) return false;
                char text[5];
                size_t count = 0;
                const unsigned int value = bytes[index];
                if (value >= 100) text[count++] = char('0' + value / 100);
                if (value >= 10) text[count++] = char('0' + value / 10 % 10);
                text[count++] = char('0' + value % 10);
                if (position + 1 != total) text[count++] = ',';
                text[count++] = position % 20 == 19 || position + 1 == total ? '\n' : ' ';
                if (!output.write(text, count)) return false;
                ++position;
            }
            return true;
        }
        bool u32(uint32_t value)
        {
            const unsigned char bytes[] = {static_cast<unsigned char>(value),
                static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value >> 16),
                static_cast<unsigned char>(value >> 24)};
            return write(bytes, sizeof(bytes));
        }
        bool finish()
        {
            if (position != total) return error("shader family size changed while writing");
            return !header || output.text("};\n");
        }
    };

    bool build_blob(const char* outputPath, const char* catalogPath, const char* headerSymbol)
    {
        const bool header = headerSymbol && *headerSymbol;
        if (header && !is_identifier(headerSymbol)) return error("invalid shader header symbol");
        Catalog catalog;
        fs::path destination;
        if (!catalog.read(catalogPath) || !make_path(outputPath, strlen(outputPath), destination)) return false;
        Output output;
        if (!output.begin(destination)) return false;
        BlobOutput blob{output, header, catalog.blobSize};
        if (!blob.start(headerSymbol)) return false;
        if (!catalog.raw() && !blob.write("NVSP", 4)) return false;
        char buffer[64 * 1024];
        for (size_t index = 0; index < catalog.count; ++index)
        {
            const Catalog::Entry& entry = catalog.entries[index];
            File object;
            uint64_t size = 0;
            if (!object.open(entry.object) || !object.length(size)) return false;
            if (size != entry.size) return error("shader object size changed while reading:", entry.object);
            if (!catalog.raw() && (!blob.u32(uint32_t(entry.keySize)) || !blob.u32(uint32_t(size)) ||
                !blob.write(entry.key, entry.keySize))) return false;
            while (size)
            {
                const size_t count = size > sizeof(buffer) ? sizeof(buffer) : size_t(size);
                if (!object.read(buffer, count) || !blob.write(buffer, count)) return false;
                size -= count;
            }
            if (!object.close()) return false;
        }
        return blob.finish() && output.publish(destination);
    }

    bool write_escaped_path(Output& output, const char* value)
    {
        for (; *value; ++value)
        {
            if ((*value == ' ' || *value == '#') && !output.text("\\")) return false;
            if (*value == '$' && !output.text("$")) return false;
            if (!output.write(value, 1)) return false;
        }
        return true;
    }

    bool publish_depfile(const fs::path& target, const fs::path& depfile, const Dependencies& dependencies)
    {
        Bytes targetText;
        if (!path_text(target.lexically_normal(), targetText, true)) return false;
        Output output;
        if (!output.begin(depfile) || !write_escaped_path(output, targetText.data) || !output.text(":")) return false;
        for (size_t index = 0; index < dependencies.count; ++index)
            if (!output.text(" \\\n  ") || !write_escaped_path(output, dependencies.entries[index].path)) return false;
        return output.text("\n") && output.publish(depfile);
    }

    bool run_dependency_scan(int argc, char** argv)
    {
        fs::path source;
        fs::path target;
        fs::path depfile;
        Directories directories;
        if (!directories.prepare(size_t(argc))) return false;
        for (int index = 2; index < argc; ++index)
        {
            const char* option = argv[index];
            if (index + 1 >= argc) return error("missing value for", option);
            const char* text = argv[++index];
            fs::path value;
            if (!make_path(text, strlen(text), value)) return false;
            if (strcmp(option, "--source") == 0) source = static_cast<fs::path&&>(value);
            else if (strcmp(option, "--target") == 0) target = static_cast<fs::path&&>(value);
            else if (strcmp(option, "--depfile") == 0) depfile = static_cast<fs::path&&>(value);
            else if (strcmp(option, "--include-directory") == 0)
                directories.paths[directories.count++] = normalized_existing_path(value);
            else return error("unknown dependency-scan option", option);
        }
        if (source.empty() || target.empty() || depfile.empty())
            return error("required shader dependency-scan option is missing");
        Dependencies dependencies;
        return scan_shader_files(source, directories, dependencies) && publish_depfile(target, depfile, dependencies);
    }

    bool run(int argc, char** argv)
    {
        if (argc > 1 && strcmp(argv[1], "--scan-dependencies") == 0) return run_dependency_scan(argc, argv);
        const char* output = nullptr;
        const char* catalog = nullptr;
        const char* headerSymbol = nullptr;
        for (int index = 1; index < argc; ++index)
        {
            const char* option = argv[index];
            if (index + 1 >= argc) return error("missing value for", option);
            const char* value = argv[++index];
            if (strcmp(option, "--output") == 0) output = value;
            else if (strcmp(option, "--catalog") == 0) catalog = value;
            else if (strcmp(option, "--header-symbol") == 0) headerSymbol = value;
            else return error("unknown option", option);
        }
        if (!output || !*output || !catalog || !*catalog)
            return error("required shader blob builder option is missing");
        return build_blob(output, catalog, headerSymbol);
    }

    struct Arguments
    {
        char** values = nullptr;
        Bytes text;
        ~Arguments() noexcept { delete[] values; }
        bool prepare(int count, wchar_t** arguments) noexcept
        {
            if (count <= 0 || size_t(count) >= size_t(PTRDIFF_MAX) / sizeof(char*))
                return error("command-line capacity exceeded");
            size_t size = 0;
            for (int index = 0; index < count; ++index)
            {
                const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                    arguments[index], -1, nullptr, 0, nullptr, nullptr);
                if (!length) return error("invalid Unicode command line");
                if (size_t(length) > size_t(PTRDIFF_MAX) - size)
                    return error("command-line capacity exceeded");
                size += size_t(length);
            }
            values = new (std::nothrow) char*[size_t(count) + 1];
            if (!values) return error("out of memory");
            if (!text.prepare(size)) return false;
            size_t offset = 0;
            for (int index = 0; index < count; ++index)
            {
                values[index] = text.data + offset;
                const int capacity = size - offset > INT_MAX ? INT_MAX : int(size - offset);
                const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                    arguments[index], -1, values[index], capacity, nullptr, nullptr);
                if (!length) return error("invalid Unicode command line");
                offset += size_t(length);
            }
            values[count] = nullptr;
            return true;
        }
    };
}

int wmain(int argc, wchar_t** argv)
{
    if (!configure_allocation_failure()) return 1;
    Arguments arguments;
    return arguments.prepare(argc, argv) && run(argc, arguments.values) ? 0 : 1;
}
