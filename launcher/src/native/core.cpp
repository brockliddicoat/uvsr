#include "internal.h"
#include "resource_ids.h"

#include <bcrypt.h>
#include <commctrl.h>
#include <knownfolders.h>
#include <lmcons.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <type_traits>

namespace fs = std::filesystem;

namespace uvsr::launcher
{
    namespace
    {
        template<typename T, auto Close>
        class Scoped final
        {
        public:
            Scoped() = default;
            explicit Scoped(T value) : m_Value(value) {}
            ~Scoped() { reset(); }
            Scoped(const Scoped&) = delete;
            Scoped& operator=(const Scoped&) = delete;
            Scoped(Scoped&& other) noexcept : m_Value(other.release()) {}
            Scoped& operator=(Scoped&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    m_Value = other.release();
                }
                return *this;
            }
            [[nodiscard]] T get() const noexcept { return m_Value; }
            [[nodiscard]] explicit operator bool() const noexcept
            {
                if constexpr (std::is_pointer_v<T>)
                    return m_Value != nullptr && m_Value != INVALID_HANDLE_VALUE;
                else
                    return m_Value != T{};
            }
            [[nodiscard]] T release() noexcept
            {
                const T value = m_Value;
                m_Value = T{};
                return value;
            }
            void reset(T value = T{}) noexcept
            {
                if (*this)
                    Close(m_Value);
                m_Value = value;
            }

        private:
            T m_Value{};
        };

        void close_algorithm(BCRYPT_ALG_HANDLE value)
        {
            (void)BCryptCloseAlgorithmProvider(value, 0);
        }

        void destroy_hash(BCRYPT_HASH_HANDLE value)
        {
            (void)BCryptDestroyHash(value);
        }

        void destroy_key(BCRYPT_KEY_HANDLE value)
        {
            (void)BCryptDestroyKey(value);
        }

        using Handle = Scoped<HANDLE, CloseHandle>;
        using Algorithm = Scoped<BCRYPT_ALG_HANDLE, close_algorithm>;
        using Hash = Scoped<BCRYPT_HASH_HANDLE, destroy_hash>;
        using Key = Scoped<BCRYPT_KEY_HANDLE, destroy_key>;

        [[nodiscard]] fs::path full_path(const fs::path& value)
        {
            if (value.empty())
                throw Error("A required Windows path was empty.");
            const DWORD needed = GetFullPathNameW(
                value.c_str(), 0, nullptr, nullptr);
            if (needed == 0)
                detail::throw_last_error("Windows could not resolve a path");
            std::wstring buffer(needed, L'\0');
            const DWORD written = GetFullPathNameW(
                value.c_str(), needed, buffer.data(), nullptr);
            if (written == 0 || written >= needed)
                detail::throw_last_error("Windows could not resolve a path");
            buffer.resize(written);
            while (buffer.size() > 3 &&
                (buffer.back() == L'\\' || buffer.back() == L'/'))
            {
                buffer.pop_back();
            }
            return fs::path(buffer).lexically_normal();
        }

        [[nodiscard]] bool ordinal_equal(
            std::wstring_view left,
            std::wstring_view right)
        {
            return CompareStringOrdinal(
                left.data(), static_cast<int>(left.size()),
                right.data(), static_cast<int>(right.size()), TRUE) ==
                CSTR_EQUAL;
        }

        [[nodiscard]] bool ordinal_starts_with(
            std::wstring_view value,
            std::wstring_view prefix)
        {
            if (value.size() < prefix.size())
                return false;
            return ordinal_equal(value.substr(0, prefix.size()), prefix);
        }

        [[nodiscard]] std::uint64_t file_time_value(const FILETIME& value)
        {
            return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32u) |
                value.dwLowDateTime;
        }

        [[nodiscard]] std::string hash_finish(
            BCRYPT_HASH_HANDLE hash)
        {
            std::array<unsigned char, 32> digest{};
            if (BCryptFinishHash(hash, digest.data(),
                    static_cast<ULONG>(digest.size()), 0) < 0)
            {
                throw Error("Windows could not finish SHA-256 verification.");
            }
            constexpr char Hex[] = "0123456789abcdef";
            std::string output(digest.size() * 2u, '\0');
            for (std::size_t index = 0; index < digest.size(); ++index)
            {
                output[index * 2u] = Hex[digest[index] >> 4u];
                output[index * 2u + 1u] = Hex[digest[index] & 0x0fu];
            }
            return output;
        }

        struct HashContext
        {
            Algorithm algorithm;
            std::vector<unsigned char> object;
            Hash hash;

            HashContext()
            {
                BCRYPT_ALG_HANDLE algorithmHandle = nullptr;
                if (BCryptOpenAlgorithmProvider(&algorithmHandle,
                        BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
                {
                    throw Error("Windows could not open SHA-256 verification.");
                }
                algorithm.reset(algorithmHandle);
                ULONG objectBytes = 0;
                ULONG copied = 0;
                if (BCryptGetProperty(algorithm.get(), BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&objectBytes),
                        sizeof(objectBytes), &copied, 0) < 0 ||
                    copied != sizeof(objectBytes) || objectBytes == 0)
                {
                    throw Error("Windows returned an invalid SHA-256 context size.");
                }
                object.resize(objectBytes);
                BCRYPT_HASH_HANDLE hashHandle = nullptr;
                if (BCryptCreateHash(algorithm.get(), &hashHandle,
                        object.data(), objectBytes, nullptr, 0, 0) < 0)
                {
                    throw Error("Windows could not create SHA-256 verification.");
                }
                hash.reset(hashHandle);
            }

            void update(std::span<const std::byte> bytes)
            {
                std::size_t offset = 0;
                while (offset < bytes.size())
                {
                    const ULONG count = static_cast<ULONG>(std::min<std::size_t>(
                        bytes.size() - offset,
                        std::numeric_limits<ULONG>::max()));
                    if (BCryptHashData(hash.get(),
                            reinterpret_cast<PUCHAR>(
                                const_cast<std::byte*>(bytes.data() + offset)),
                            count, 0) < 0)
                    {
                        throw Error("Windows could not update SHA-256 verification.");
                    }
                    offset += count;
                }
            }
        };

        [[nodiscard]] std::string optional_indent(int count)
        {
            return std::string(static_cast<std::size_t>(std::max(0, count)), ' ');
        }

        [[nodiscard]] std::wstring known_folder(REFKNOWNFOLDERID id)
        {
            PWSTR raw = nullptr;
            const HRESULT result = SHGetKnownFolderPath(
                id, KF_FLAG_CREATE, nullptr, &raw);
            if (FAILED(result) || raw == nullptr)
                throw Error("Windows could not locate a required user folder.");
            std::wstring value(raw);
            CoTaskMemFree(raw);
            if (value.empty())
                throw Error("Windows returned an empty required user folder.");
            return value;
        }

        [[nodiscard]] bool parse_numeric_version(
            std::string_view value,
            std::size_t count,
            std::uint64_t maximum)
        {
            std::size_t start = 0;
            for (std::size_t index = 0; index < count; ++index)
            {
                const std::size_t end = value.find('.', start);
                const bool last = index + 1u == count;
                if ((last && end != std::string_view::npos) ||
                    (!last && end == std::string_view::npos))
                {
                    return false;
                }
                const std::string_view part = value.substr(
                    start, (last ? value.size() : end) - start);
                if (part.empty() || (part.size() > 1u && part.front() == '0'))
                    return false;
                std::uint64_t number = 0;
                const auto parsed = std::from_chars(
                    part.data(), part.data() + part.size(), number);
                if (parsed.ec != std::errc{} ||
                    parsed.ptr != part.data() + part.size() || number > maximum)
                {
                    return false;
                }
                start = end + 1u;
            }
            return true;
        }

        [[nodiscard]] std::string file_version_string(
            const fs::path& path,
            std::wstring_view field)
        {
            DWORD ignored = 0;
            const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
            if (size == 0)
                throw Error("The launcher file had no Windows version metadata.");
            std::vector<std::byte> data(size);
            if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
                detail::throw_last_error("Windows could not read launcher metadata");
            struct Translation { WORD language; WORD codePage; };
            Translation* translations = nullptr;
            UINT translationBytes = 0;
            if (!VerQueryValueW(data.data(),
                    L"\\VarFileInfo\\Translation",
                    reinterpret_cast<void**>(&translations),
                    &translationBytes) ||
                translationBytes < sizeof(Translation))
            {
                throw Error("The launcher file had incomplete Windows metadata.");
            }
            wchar_t query[128]{};
            swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%.*s",
                translations[0].language, translations[0].codePage,
                static_cast<int>(field.size()), field.data());
            wchar_t* result = nullptr;
            UINT characters = 0;
            if (!VerQueryValueW(data.data(), query,
                    reinterpret_cast<void**>(&result), &characters) ||
                result == nullptr || characters <= 1)
            {
                throw Error("The launcher file had incomplete Windows metadata.");
            }
            return narrow(std::wstring_view(result, characters - 1u));
        }
    }

    void Cancellation::request() noexcept
    {
        m_Requested.store(true, std::memory_order_release);
    }

    bool Cancellation::requested() const noexcept
    {
        return m_Requested.load(std::memory_order_acquire);
    }

    void Cancellation::check() const
    {
        if (requested())
            throw Cancelled();
    }

    std::wstring widen(std::string_view value)
    {
        if (value.empty())
            return {};
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (length <= 0)
            throw Error("Text was not valid UTF-8.");
        std::wstring output(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                value.data(), static_cast<int>(value.size()),
                output.data(), length) != length)
        {
            throw Error("Windows could not convert UTF-8 text.");
        }
        return output;
    }

    std::string narrow(std::wstring_view value)
    {
        if (value.empty())
            return {};
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            value.data(), static_cast<int>(value.size()), nullptr, 0,
            nullptr, nullptr);
        if (length <= 0)
            throw Error("Text was not valid Unicode.");
        std::string output(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                value.data(), static_cast<int>(value.size()),
                output.data(), length, nullptr, nullptr) != length)
        {
            throw Error("Windows could not convert Unicode text.");
        }
        return output;
    }

    std::string guid_string(const GUID& value)
    {
        wchar_t buffer[40]{};
        const int length = StringFromGUID2(value, buffer, 40);
        if (length <= 2)
            throw Error("Windows could not format an installation identity.");
        std::wstring result(buffer + 1, buffer + length - 2);
        std::transform(result.begin(), result.end(), result.begin(),
            [](const wchar_t character)
            {
                return static_cast<wchar_t>(towlower(character));
            });
        return narrow(result);
    }

    GUID parse_guid(std::string_view value)
    {
        const std::wstring text = L"{" + widen(value) + L"}";
        GUID result{};
        if (FAILED(CLSIDFromString(text.c_str(), &result)))
            throw Error("A UVSR installation identity was invalid.");
        return result;
    }

    bool guid_equal(const GUID& left, const GUID& right) noexcept
    {
        return InlineIsEqualGUID(left, right) != FALSE;
    }

    std::string sha256(const fs::path& path)
    {
        Handle file(CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!file)
            detail::throw_last_error("Windows could not open a file for SHA-256");
        HashContext context;
        std::vector<std::byte> buffer(1024u * 1024u);
        for (;;)
        {
            DWORD read = 0;
            if (!ReadFile(file.get(), buffer.data(),
                    static_cast<DWORD>(buffer.size()), &read, nullptr))
            {
                detail::throw_last_error("Windows could not read a file for SHA-256");
            }
            if (read == 0)
                break;
            context.update(std::span(buffer.data(), read));
        }
        return hash_finish(context.hash.get());
    }

    std::string sha256(std::span<const std::byte> bytes)
    {
        HashContext context;
        context.update(bytes);
        return hash_finish(context.hash.get());
    }

    bool is_lower_hex(std::string_view value, std::size_t length)
    {
        if (value.size() != length)
            return false;
        return std::all_of(value.begin(), value.end(), [](const char character)
        {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f');
        });
    }

    bool is_stable_version(std::string_view value)
    {
        return parse_numeric_version(
            value, 3u, static_cast<std::uint64_t>(INT_MAX));
    }

    bool is_engine_version(std::string_view value)
    {
        return parse_numeric_version(value, 4u, USHRT_MAX);
    }

    bool is_version_id(std::string_view value)
    {
        if (value.size() != 64u || value[40] != '-' || value[55] != '-')
            return false;
        if (!is_lower_hex(value.substr(0, 40), 40) ||
            !is_lower_hex(value.substr(56), 8))
        {
            return false;
        }
        return std::all_of(value.begin() + 41, value.begin() + 55,
            [](const char character)
            {
                return character >= '0' && character <= '9';
            });
    }

    bool is_strict_descendant(const fs::path& candidate, const fs::path& parent)
    {
        const std::wstring child = full_path(candidate).native();
        std::wstring root = full_path(parent).native();
        if (ordinal_equal(child, root))
            return false;
        if (root.back() != L'\\')
            root.push_back(L'\\');
        return ordinal_starts_with(child, root);
    }

    void reject_reparse_chain(const fs::path& path, std::string_view description)
    {
        fs::path current = full_path(path);
        for (;;)
        {
            const DWORD attributes = GetFileAttributesW(current.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                throw Error("The " + std::string(description) +
                    " used a filesystem link and was preserved.");
            }
            const fs::path parent = current.parent_path();
            if (parent.empty() || parent == current)
                break;
            current = parent;
        }
    }

    void delete_owned_tree(const fs::path& path, const fs::path& parent)
    {
        if (!is_strict_descendant(path, parent))
            throw Error("A managed cleanup path escaped its owned directory.");
        if (!fs::exists(path))
            return;
        reject_reparse_chain(path, "managed cleanup directory");
        for (const auto& entry : fs::recursive_directory_iterator(path))
        {
            const DWORD attributes = GetFileAttributesW(entry.path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
                detail::throw_last_error("Windows could not inspect managed files");
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                throw Error("A managed cleanup tree contained a filesystem link and was preserved.");
        }
        std::error_code error;
        fs::remove_all(path, error);
        if (error || fs::exists(path))
            throw Error("Windows could not remove an owned UVSR directory.");
    }

    std::vector<std::byte> resource_bytes(int id)
    {
        HRSRC resource = FindResourceW(GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(id), RT_RCDATA);
        if (resource == nullptr)
            throw Error("The launcher resource bundle was incomplete.");
        HGLOBAL loaded = LoadResource(GetModuleHandleW(nullptr), resource);
        const DWORD size = SizeofResource(GetModuleHandleW(nullptr), resource);
        const void* data = loaded == nullptr ? nullptr : LockResource(loaded);
        if (data == nullptr || size == 0)
            throw Error("The launcher resource bundle was invalid.");
        const auto* first = static_cast<const std::byte*>(data);
        return { first, first + size };
    }

    std::string resource_text(int id)
    {
        const auto bytes = resource_bytes(id);
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    void ensure_fonts()
    {
        struct FontRegistration
        {
            std::vector<std::byte> regular;
            std::vector<std::byte> bold;
            HANDLE regularHandle = nullptr;
            HANDLE boldHandle = nullptr;

            FontRegistration()
            {
                regular = resource_bytes(IDR_NOTO_REGULAR);
                bold = resource_bytes(IDR_NOTO_BOLD);
                if (regular.size() != 621572u ||
                    sha256(regular) !=
                        "478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823" ||
                    bold.size() != 631484u ||
                    sha256(bold) !=
                        "1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913")
                {
                    throw Error("The embedded Noto Sans fonts failed their integrity check.");
                }
                DWORD regularCount = 0;
                DWORD boldCount = 0;
                regularHandle = AddFontMemResourceEx(
                    regular.data(), static_cast<DWORD>(regular.size()),
                    nullptr, &regularCount);
                boldHandle = AddFontMemResourceEx(
                    bold.data(), static_cast<DWORD>(bold.size()),
                    nullptr, &boldCount);
                if (regularHandle == nullptr || boldHandle == nullptr ||
                    regularCount == 0 || boldCount == 0)
                {
                    if (regularHandle != nullptr)
                        RemoveFontMemResourceEx(regularHandle);
                    if (boldHandle != nullptr)
                        RemoveFontMemResourceEx(boldHandle);
                    throw Error("Windows could not register the embedded Noto Sans fonts.");
                }
            }

            ~FontRegistration()
            {
                if (regularHandle != nullptr)
                    RemoveFontMemResourceEx(regularHandle);
                if (boldHandle != nullptr)
                    RemoveFontMemResourceEx(boldHandle);
            }
        };
        static FontRegistration fonts;
        (void)fonts;
    }

    InstallerPaths InstallerPaths::current_user()
    {
        return create(known_folder(FOLDERID_LocalAppData),
            known_folder(FOLDERID_Desktop),
            known_folder(FOLDERID_Programs));
    }

    InstallerPaths InstallerPaths::create(
        fs::path local,
        fs::path desktop,
        fs::path programs)
    {
        local = full_path(local);
        desktop = full_path(desktop);
        programs = full_path(programs);
        reject_reparse_chain(local, "local application data directory");
        InstallerPaths result{
            local,
            full_path(local / L"Programs" / L"UVSR"),
            full_path(local / L"UVSR Installer"),
            desktop,
            programs
        };
        if (!is_strict_descendant(result.programRoot, local) ||
            !is_strict_descendant(result.stateRoot, local) ||
            detail::path_equal(result.programRoot, result.stateRoot))
        {
            throw Error("Windows returned an unsafe application data location.");
        }
        reject_reparse_chain(result.programRoot, "UVSR program directory");
        reject_reparse_chain(result.stateRoot, "UVSR installer state directory");
        reject_reparse_chain(result.operations_root(),
            "UVSR cleanup coordination directory");
        return result;
    }

    fs::path InstallerPaths::program_marker() const
    { return programRoot / L".uvsr-installer-owner.json"; }
    fs::path InstallerPaths::state_marker() const
    { return stateRoot / L".uvsr-installer-owner.json"; }
    fs::path InstallerPaths::state_file() const
    { return stateRoot / L"state.json"; }
    fs::path InstallerPaths::transaction_file() const
    { return stateRoot / L"transaction.json"; }
    fs::path InstallerPaths::logs_directory() const
    { return stateRoot / L"logs"; }
    fs::path InstallerPaths::downloads_directory() const
    { return stateRoot / L"downloads"; }
    fs::path InstallerPaths::staging_directory() const
    { return programRoot / L"staging"; }
    fs::path InstallerPaths::versions_directory() const
    { return programRoot / L"versions"; }
    fs::path InstallerPaths::launcher_versions_directory() const
    { return programRoot / L"launcher" / L"versions"; }
    fs::path InstallerPaths::launcher_staging_directory() const
    { return programRoot / L"launcher" / L"staging"; }
    fs::path InstallerPaths::launcher_state_file() const
    { return stateRoot / L"launcher-state.json"; }
    fs::path InstallerPaths::launcher_transaction_file() const
    { return stateRoot / L"launcher-update.json"; }
    fs::path InstallerPaths::desktop_shortcut() const
    { return desktopDirectory / L"UVSR Launcher.lnk"; }
    fs::path InstallerPaths::start_menu_directory() const
    { return programsDirectory / L"UVSR"; }
    fs::path InstallerPaths::start_menu_shortcut() const
    { return start_menu_directory() / L"UVSR Launcher.lnk"; }
    fs::path InstallerPaths::operations_root() const
    { return localApplicationData / L"UVSR Installer Operations"; }
    fs::path InstallerPaths::operations_marker() const
    { return operations_root() / L".uvsr-installer-owner.json"; }
    fs::path InstallerPaths::uninstall_record_file() const
    { return operations_root() / L"uninstall.json"; }
    fs::path InstallerPaths::helpers_directory() const
    { return operations_root() / L"helpers"; }

    fs::path InstallerPaths::version_root(std::string_view versionId) const
    {
        if (!is_version_id(versionId))
            throw Error("The installed UVSR version identifier was invalid.");
        return versions_directory() / widen(versionId);
    }

    fs::path InstallerPaths::version_executable(std::string_view versionId) const
    {
        return version_root(versionId) / L"bin" / EngineExecutableName;
    }

    fs::path InstallerPaths::launcher_version_root(std::string_view hash) const
    {
        if (!is_lower_hex(hash, 64u))
            throw Error("The launcher package identifier was invalid.");
        return launcher_versions_directory() / widen(hash);
    }

    fs::path InstallerPaths::launcher_executable(std::string_view hash) const
    {
        return launcher_version_root(hash) / LauncherExecutableName;
    }

    fs::path InstallerPaths::launcher_package_marker(std::string_view hash) const
    {
        return launcher_version_root(hash) / L".uvsr-launcher-package.json";
    }
}

namespace uvsr::launcher::detail
{
    std::string win32_message(DWORD error)
    {
        wchar_t* raw = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, error, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
        std::wstring message = length == 0 || raw == nullptr
            ? L"Windows error " + std::to_wstring(error)
            : std::wstring(raw, length);
        if (raw != nullptr)
            LocalFree(raw);
        while (!message.empty() &&
            (message.back() == L'\r' || message.back() == L'\n' ||
                message.back() == L' ' || message.back() == L'.'))
        {
            message.pop_back();
        }
        return narrow(message);
    }

    [[noreturn]] void throw_last_error(std::string_view operation)
    {
        const DWORD error = GetLastError();
        throw Error(std::string(operation) + ": " + win32_message(error));
    }

    bool path_equal(const fs::path& left, const fs::path& right)
    {
        return ordinal_equal(full_path(left).native(), full_path(right).native());
    }

    std::vector<std::byte> read_file(
        const fs::path& path,
        std::uint64_t maximumBytes,
        bool allowEmpty)
    {
        reject_reparse_chain(path, "launcher record");
        std::error_code error;
        const std::uint64_t size = fs::file_size(path, error);
        if (error || (!allowEmpty && size == 0) || size > maximumBytes ||
            size > static_cast<std::uint64_t>(SIZE_MAX))
        {
            throw Error("The UVSR Launcher record '" +
                narrow(path.filename().native()) + "' was missing or invalid.");
        }
        Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!file)
            throw_last_error("Windows could not read a launcher record");
        std::vector<std::byte> result(static_cast<std::size_t>(size));
        std::size_t offset = 0;
        while (offset < result.size())
        {
            DWORD read = 0;
            const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
                result.size() - offset, 1024u * 1024u));
            if (!ReadFile(file.get(), result.data() + offset, request,
                    &read, nullptr) || read == 0)
            {
                throw_last_error("Windows could not finish reading a launcher record");
            }
            offset += read;
        }
        return result;
    }

    std::string read_text(
        const fs::path& path,
        std::uint64_t maximumBytes,
        bool allowEmpty)
    {
        const auto bytes = read_file(path, maximumBytes, allowEmpty);
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    void write_atomic(const fs::path& path, std::string_view contents)
    {
        const fs::path parent = path.parent_path();
        if (parent.empty())
            throw Error("A UVSR Launcher record had no parent directory.");
        reject_reparse_chain(parent, "launcher record directory");
        fs::create_directories(parent);
        reject_reparse_chain(parent, "launcher record directory");
        reject_reparse_chain(path, "launcher record destination");
        const fs::path temporary = parent /
            (L"." + path.filename().native() + L"." +
                widen(guid_string(new_guid())) + L".tmp");
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
        if (!file)
            throw_last_error("Windows could not create a launcher record");
        try
        {
            std::size_t offset = 0;
            while (offset < contents.size())
            {
                DWORD written = 0;
                const DWORD count = static_cast<DWORD>(std::min<std::size_t>(
                    contents.size() - offset, 1024u * 1024u));
                if (!WriteFile(file.get(), contents.data() + offset, count,
                        &written, nullptr) || written != count)
                {
                    throw_last_error("Windows could not write a launcher record");
                }
                offset += written;
            }
            if (!FlushFileBuffers(file.get()))
                throw_last_error("Windows could not flush a launcher record");
            file.reset();
            if (!MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                throw_last_error("Windows could not activate a launcher record");
            }
        }
        catch (...)
        {
            file.reset();
            DeleteFileW(temporary.c_str());
            throw;
        }
    }

    void copy_file_verified(
        const fs::path& source,
        const fs::path& destination,
        std::string_view expectedHash)
    {
        fs::create_directories(destination.parent_path());
        if (!CopyFileW(source.c_str(), destination.c_str(), TRUE))
            throw_last_error("Windows could not copy the UVSR Launcher");
        if (sha256(destination) != expectedHash)
        {
            DeleteFileW(destination.c_str());
            throw Error("The UVSR Launcher did not copy correctly.");
        }
    }

    fs::path current_executable()
    {
        std::wstring buffer(32768u, L'\0');
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
            throw_last_error("Windows could not locate the running UVSR Launcher");
        buffer.resize(length);
        return full_path(buffer);
    }

    std::string now_iso8601()
    {
        SYSTEMTIME time{};
        GetSystemTime(&time);
        char buffer[40]{};
        sprintf_s(buffer, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
            time.wYear, time.wMonth, time.wDay, time.wHour,
            time.wMinute, time.wSecond, time.wMilliseconds);
        return buffer;
    }

    GUID new_guid()
    {
        GUID value{};
        if (FAILED(CoCreateGuid(&value)))
            throw Error("Windows could not create a UVSR transaction identity.");
        return value;
    }

    std::string json_escape(std::string_view value)
    {
        std::string output;
        output.reserve(value.size() + 8u);
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (character < 0x20u)
                {
                    char escaped[7]{};
                    sprintf_s(escaped, "\\u%04x", character);
                    output += escaped;
                }
                else
                    output.push_back(static_cast<char>(character));
                break;
            }
        }
        return output;
    }

    Json parse_json(std::string_view text)
    {
        try
        {
            return uvsr::json::Parse(text);
        }
        catch (const std::exception& error)
        {
            throw Error(std::string("A UVSR JSON record was invalid. ") +
                error.what());
        }
    }

    void require_exact_object(
        const Json& value,
        std::initializer_list<std::string_view> names,
        std::string_view description)
    {
        if (value.kind != Json::Kind::Object || value.object.size() != names.size())
            throw Error(std::string(description) + " had unexpected properties.");
        for (const auto name : names)
        {
            if (value.Find(name) == nullptr)
                throw Error(std::string(description) + " was missing " +
                    std::string(name) + ".");
        }
    }

    const Json& member(const Json& value, std::string_view name)
    {
        const Json* result = value.Find(name);
        if (result == nullptr)
            throw Error("A UVSR JSON record was missing " + std::string(name) + ".");
        return *result;
    }

    std::string string_value(const Json& value, std::string_view description)
    {
        if (value.kind != Json::Kind::String)
            throw Error(std::string(description) + " was not text.");
        return value.string;
    }

    std::int64_t integer_value(const Json& value, std::string_view description)
    {
        constexpr double MaximumExactInteger = 9007199254740991.0;
        if (value.kind != Json::Kind::Number ||
            value.number < -MaximumExactInteger ||
            value.number > MaximumExactInteger ||
            std::trunc(value.number) != value.number)
        {
            throw Error(std::string(description) + " was not an integer.");
        }
        const auto result = static_cast<std::int64_t>(value.number);
        if (static_cast<double>(result) != value.number)
            throw Error(std::string(description) + " was not an exact integer.");
        return result;
    }

    std::uint64_t unsigned_value(const Json& value, std::string_view description)
    {
        const std::int64_t result = integer_value(value, description);
        if (result < 0)
            throw Error(std::string(description) + " was negative.");
        return static_cast<std::uint64_t>(result);
    }

    bool boolean_value(const Json& value, std::string_view description)
    {
        if (value.kind != Json::Kind::Boolean)
            throw Error(std::string(description) + " was not a Boolean.");
        return value.boolean;
    }

    std::optional<std::string> nullable_string(
        const Json& value,
        std::string_view description)
    {
        if (value.kind == Json::Kind::Null)
            return std::nullopt;
        return string_value(value, description);
    }

    void validate_owner(const OwnerMarker& marker)
    {
        if (marker.schemaVersion != SchemaVersion || marker.productId != ProductId ||
            guid_equal(marker.installationId, GUID{}))
        {
            throw Error("An existing UVSR directory had an unknown owner. It was preserved.");
        }
    }

    void validate_install_state(
        const InstallState& state,
        const GUID& expectedInstallationId)
    {
        if (state.schemaVersion != SchemaVersion ||
            !guid_equal(state.installationId, expectedInstallationId) ||
            !is_version_id(state.activeVersionId) ||
            state.releaseSequence < 1 ||
            state.releaseSequence > MaximumReleaseSequence ||
            !is_lower_hex(state.commit, 40u) ||
            !is_lower_hex(state.settingsHash, 32u) ||
            !is_engine_version(state.engineVersion) ||
            !is_lower_hex(state.artifactSha256, 64u) ||
            !is_lower_hex(state.executableSha256, 64u) ||
            state.installedUtc.empty())
        {
            throw Error("The installed UVSR record was invalid. No files were changed.");
        }
    }

    void validate_launcher_state(
        const LauncherState& state,
        const GUID& expectedInstallationId)
    {
        if (state.schemaVersion != LauncherSchemaVersion ||
            state.productId != ProductId ||
            !guid_equal(state.installationId, expectedInstallationId) ||
            state.releaseSequence < 1 ||
            state.releaseSequence > MaximumReleaseSequence ||
            !is_stable_version(state.version) ||
            !is_lower_hex(state.executableSha256, 64u) ||
            state.installedUtc.empty())
        {
            throw Error("The installed UVSR Launcher record was invalid.");
        }
    }

    OwnerMarker parse_owner(std::string_view text)
    {
        const Json root = parse_json(text);
        require_exact_object(root,
            { "schemaVersion", "productId", "installationId" },
            "The UVSR ownership record");
        OwnerMarker result;
        result.schemaVersion = static_cast<int>(integer_value(
            member(root, "schemaVersion"), "Ownership schema version"));
        result.productId = string_value(member(root, "productId"), "Product ID");
        result.installationId = parse_guid(string_value(
            member(root, "installationId"), "Installation ID"));
        validate_owner(result);
        return result;
    }

    InstallState parse_install_state(const Json& root)
    {
        require_exact_object(root,
            { "schemaVersion", "installationId", "activeVersionId",
                "releaseSequence", "commit", "settingsHash", "engineVersion",
                "artifactSha256", "executableSha256", "desktopShortcut",
                "installedUtc" },
            "The UVSR installed state");
        InstallState state;
        state.schemaVersion = static_cast<int>(integer_value(
            member(root, "schemaVersion"), "State schema version"));
        state.installationId = parse_guid(string_value(
            member(root, "installationId"), "Installation ID"));
        state.activeVersionId = string_value(
            member(root, "activeVersionId"), "Active version ID");
        state.releaseSequence = integer_value(
            member(root, "releaseSequence"), "Release sequence");
        state.commit = string_value(member(root, "commit"), "Source commit");
        state.settingsHash = string_value(
            member(root, "settingsHash"), "Settings hash");
        state.engineVersion = string_value(
            member(root, "engineVersion"), "Engine version");
        state.artifactSha256 = string_value(
            member(root, "artifactSha256"), "Artifact SHA-256");
        state.executableSha256 = string_value(
            member(root, "executableSha256"), "Executable SHA-256");
        state.desktopShortcut = boolean_value(
            member(root, "desktopShortcut"), "Desktop shortcut preference");
        state.installedUtc = string_value(
            member(root, "installedUtc"), "Installation time");
        return state;
    }

    InstallState parse_install_state(std::string_view text)
    {
        return parse_install_state(parse_json(text));
    }

    LauncherState parse_launcher_state(const Json& root)
    {
        require_exact_object(root,
            { "schemaVersion", "productId", "installationId",
                "releaseSequence", "version", "executableSha256",
                "desktopShortcut", "installedUtc" },
            "The launcher installed state");
        LauncherState state;
        state.schemaVersion = static_cast<int>(integer_value(
            member(root, "schemaVersion"), "Launcher schema version"));
        state.productId = string_value(member(root, "productId"), "Product ID");
        state.installationId = parse_guid(string_value(
            member(root, "installationId"), "Installation ID"));
        state.releaseSequence = integer_value(
            member(root, "releaseSequence"), "Release sequence");
        state.version = string_value(member(root, "version"), "Launcher version");
        state.executableSha256 = string_value(
            member(root, "executableSha256"), "Launcher SHA-256");
        state.desktopShortcut = boolean_value(
            member(root, "desktopShortcut"), "Desktop shortcut preference");
        state.installedUtc = string_value(
            member(root, "installedUtc"), "Installation time");
        return state;
    }

    LauncherState parse_launcher_state(std::string_view text)
    {
        return parse_launcher_state(parse_json(text));
    }

    std::string serialize_owner(const OwnerMarker& marker)
    {
        validate_owner(marker);
        return "{\n  \"schemaVersion\": " +
            std::to_string(marker.schemaVersion) +
            ",\n  \"productId\": \"" + json_escape(marker.productId) +
            "\",\n  \"installationId\": \"" +
            guid_string(marker.installationId) + "\"\n}\n";
    }

    std::string serialize_install_state_value(
        const std::optional<InstallState>& optional,
        int indent)
    {
        if (!optional)
            return "null";
        const InstallState& state = *optional;
        validate_install_state(state, state.installationId);
        const std::string pad = optional_indent(indent);
        const std::string inner = optional_indent(indent + 2);
        return "{\n" + inner + "\"schemaVersion\": " +
            std::to_string(state.schemaVersion) + ",\n" + inner +
            "\"installationId\": \"" + guid_string(state.installationId) +
            "\",\n" + inner + "\"activeVersionId\": \"" +
            json_escape(state.activeVersionId) + "\",\n" + inner +
            "\"releaseSequence\": " + std::to_string(state.releaseSequence) +
            ",\n" + inner + "\"commit\": \"" + json_escape(state.commit) +
            "\",\n" + inner + "\"settingsHash\": \"" +
            json_escape(state.settingsHash) + "\",\n" + inner +
            "\"engineVersion\": \"" + json_escape(state.engineVersion) +
            "\",\n" + inner + "\"artifactSha256\": \"" +
            json_escape(state.artifactSha256) + "\",\n" + inner +
            "\"executableSha256\": \"" +
            json_escape(state.executableSha256) + "\",\n" + inner +
            "\"desktopShortcut\": " +
            (state.desktopShortcut ? "true" : "false") + ",\n" + inner +
            "\"installedUtc\": \"" + json_escape(state.installedUtc) +
            "\"\n" + pad + "}";
    }

    std::string serialize_install_state(const InstallState& state)
    {
        return serialize_install_state_value(state) + "\n";
    }

    std::string serialize_launcher_state_value(
        const std::optional<LauncherState>& optional,
        int indent)
    {
        if (!optional)
            return "null";
        const LauncherState& state = *optional;
        validate_launcher_state(state, state.installationId);
        const std::string pad = optional_indent(indent);
        const std::string inner = optional_indent(indent + 2);
        return "{\n" + inner + "\"schemaVersion\": " +
            std::to_string(state.schemaVersion) + ",\n" + inner +
            "\"productId\": \"" + json_escape(state.productId) +
            "\",\n" + inner + "\"installationId\": \"" +
            guid_string(state.installationId) + "\",\n" + inner +
            "\"releaseSequence\": " + std::to_string(state.releaseSequence) +
            ",\n" + inner + "\"version\": \"" +
            json_escape(state.version) + "\",\n" + inner +
            "\"executableSha256\": \"" +
            json_escape(state.executableSha256) + "\",\n" + inner +
            "\"desktopShortcut\": " +
            (state.desktopShortcut ? "true" : "false") + ",\n" + inner +
            "\"installedUtc\": \"" + json_escape(state.installedUtc) +
            "\"\n" + pad + "}";
    }

    std::string serialize_launcher_state(const LauncherState& state)
    {
        return serialize_launcher_state_value(state) + "\n";
    }

    std::uint64_t available_bytes(const fs::path& path)
    {
        ULARGE_INTEGER available{};
        if (!GetDiskFreeSpaceExW(path.c_str(), &available, nullptr, nullptr))
            throw_last_error("Windows could not inspect free disk space");
        return available.QuadPart;
    }

    std::uint64_t directory_bytes(const fs::path& path)
    {
        std::uint64_t total = 0;
        if (!fs::exists(path))
            return 0;
        for (const auto& entry : fs::recursive_directory_iterator(path))
        {
            if (!entry.is_regular_file())
                continue;
            const auto size = entry.file_size();
            if (size > std::numeric_limits<std::uint64_t>::max() - total)
                return std::numeric_limits<std::uint64_t>::max();
            total += size;
        }
        return total;
    }

    void validate_shell_path(
        const InstallerPaths& paths,
        const fs::path& path,
        std::string_view description)
    {
        if (path_equal(path, paths.desktopDirectory) ||
            is_strict_descendant(path, paths.desktopDirectory) ||
            path_equal(path, paths.programsDirectory) ||
            is_strict_descendant(path, paths.programsDirectory))
        {
            reject_reparse_chain(path, description);
            return;
        }
        throw Error("The " + std::string(description) +
            " escaped its Windows known folder boundary.");
    }

    std::string format_bytes(std::uint64_t bytes)
    {
        std::ostringstream output;
        output << std::fixed << std::setprecision(1);
        if (bytes >= 1024ull * 1024ull * 1024ull)
            output << bytes / (1024.0 * 1024.0 * 1024.0) << " GB";
        else if (bytes >= 1024ull * 1024ull)
            output << bytes / (1024.0 * 1024.0) << " MB";
        else
            output << bytes / 1024.0 << " KB";
        return output.str();
    }

    std::string version_string(
        const fs::path& path,
        std::wstring_view field)
    {
        return file_version_string(path, field);
    }
}

namespace uvsr::launcher
{
    namespace
    {
        [[nodiscard]] std::vector<std::byte> decode_base64(
            std::string_view encoded,
            std::string_view description)
        {
            if (encoded.empty() || encoded.size() % 4u != 0u)
                throw Error(std::string(description) + " was not canonical Base64.");
            DWORD bytes = 0;
            if (!CryptStringToBinaryA(encoded.data(),
                    static_cast<DWORD>(encoded.size()),
                    CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                    nullptr, &bytes, nullptr, nullptr) || bytes == 0)
            {
                throw Error(std::string(description) + " was not valid Base64.");
            }
            std::vector<std::byte> result(bytes);
            if (!CryptStringToBinaryA(encoded.data(),
                    static_cast<DWORD>(encoded.size()),
                    CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                    reinterpret_cast<BYTE*>(result.data()), &bytes,
                    nullptr, nullptr))
            {
                throw Error(std::string(description) + " was not valid Base64.");
            }
            result.resize(bytes);
            DWORD characters = 0;
            if (!CryptBinaryToStringA(
                    reinterpret_cast<const BYTE*>(result.data()), bytes,
                    CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                    nullptr, &characters) || characters <= 1)
            {
                throw Error("Windows could not confirm canonical Base64.");
            }
            std::string roundTrip(characters, '\0');
            if (!CryptBinaryToStringA(
                    reinterpret_cast<const BYTE*>(result.data()), bytes,
                    CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                    roundTrip.data(), &characters))
            {
                throw Error("Windows could not confirm canonical Base64.");
            }
            if (!roundTrip.empty() && roundTrip.back() == '\0')
                roundTrip.pop_back();
            if (roundTrip != encoded)
                throw Error(std::string(description) + " was not canonical Base64.");
            return result;
        }

        void verify_p256_signature(
            std::span<const std::byte> payload,
            std::span<const std::byte> signature,
            std::span<const std::byte> publicKey)
        {
            if (signature.size() != 64u || publicKey.empty())
                throw Error("The update feed trust material was invalid.");
            CERT_PUBLIC_KEY_INFO* decoded = nullptr;
            DWORD decodedBytes = 0;
            if (!CryptDecodeObjectEx(X509_ASN_ENCODING,
                    X509_PUBLIC_KEY_INFO,
                    reinterpret_cast<const BYTE*>(publicKey.data()),
                    static_cast<DWORD>(publicKey.size()),
                    CRYPT_DECODE_ALLOC_FLAG, nullptr, &decoded, &decodedBytes) ||
                decoded == nullptr)
            {
                throw Error("The update feed signing key could not be decoded.");
            }
            BCRYPT_KEY_HANDLE rawKey = nullptr;
            const BOOL imported = CryptImportPublicKeyInfoEx2(
                X509_ASN_ENCODING, decoded, 0, nullptr, &rawKey);
            LocalFree(decoded);
            if (!imported || rawKey == nullptr)
                throw Error("The update feed signing key could not be imported.");
            Key key(rawKey);

            ULONG blobBytes = 0;
            if (BCryptExportKey(key.get(), nullptr, BCRYPT_ECCPUBLIC_BLOB,
                    nullptr, 0, &blobBytes, 0) < 0 ||
                blobBytes < sizeof(BCRYPT_ECCKEY_BLOB))
            {
                throw Error("The update feed signing key had an invalid curve.");
            }
            std::vector<std::byte> blob(blobBytes);
            if (BCryptExportKey(key.get(), nullptr, BCRYPT_ECCPUBLIC_BLOB,
                    reinterpret_cast<PUCHAR>(blob.data()), blobBytes,
                    &blobBytes, 0) < 0)
            {
                throw Error("The update feed signing key had an invalid curve.");
            }
            const auto* header = reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(
                blob.data());
            if (header->dwMagic != BCRYPT_ECDSA_PUBLIC_P256_MAGIC ||
                header->cbKey != 32u)
            {
                throw Error("The update feed signing key was not one P-256 key.");
            }

            HashContext hash;
            hash.update(payload);
            std::array<unsigned char, 32> digest{};
            if (BCryptFinishHash(hash.hash.get(), digest.data(),
                    static_cast<ULONG>(digest.size()), 0) < 0)
            {
                throw Error("Windows could not hash the update feed payload.");
            }
            const NTSTATUS status = BCryptVerifySignature(key.get(), nullptr,
                digest.data(), static_cast<ULONG>(digest.size()),
                reinterpret_cast<PUCHAR>(const_cast<std::byte*>(signature.data())),
                static_cast<ULONG>(signature.size()), 0);
            if (status < 0)
                throw Error("The update feed signature was invalid. No update was trusted.");
        }

        struct VerifiedPayload
        {
            std::vector<std::byte> bytes;
            detail::Json json;
        };

        [[nodiscard]] VerifiedPayload verify_envelope(
            std::span<const std::byte> envelope,
            int expectedSchema,
            std::span<const std::byte> suppliedKey,
            std::string_view suppliedKeyId,
            std::string_view component)
        {
            if (envelope.empty() || envelope.size() > MaximumFeedBytes)
                throw Error("The " + std::string(component) +
                    " update feed envelope was empty or too large.");
            const std::string text(
                reinterpret_cast<const char*>(envelope.data()), envelope.size());
            const detail::Json root = detail::parse_json(text);
            detail::require_exact_object(root,
                { "schemaVersion", "keyId", "payloadBase64", "signatureBase64" },
                "The update feed envelope");
            const int schema = static_cast<int>(detail::integer_value(
                detail::member(root, "schemaVersion"), "Feed schema version"));
            const std::string keyId = detail::string_value(
                detail::member(root, "keyId"), "Feed key ID");
            const std::string payloadBase64 = detail::string_value(
                detail::member(root, "payloadBase64"), "Feed payload");
            const std::string signatureBase64 = detail::string_value(
                detail::member(root, "signatureBase64"), "Feed signature");
            const std::string_view trustedId = suppliedKeyId.empty()
                ? UpdateFeedKeyId
                : suppliedKeyId;
            if (schema != expectedSchema || keyId != trustedId)
                throw Error("The " + std::string(component) +
                    " update feed trust identity was not accepted.");
            std::vector<std::byte> trusted;
            if (suppliedKey.empty())
                trusted = decode_base64(UpdateFeedPublicKeySpkiBase64,
                    "The pinned update key");
            const std::span<const std::byte> key = suppliedKey.empty()
                ? std::span<const std::byte>(trusted)
                : suppliedKey;
            auto payload = decode_base64(payloadBase64, "The update feed payload");
            if (payload.empty() || payload.size() > MaximumFeedPayloadBytes)
                throw Error("The update feed payload was empty or too large.");
            const auto signature = decode_base64(
                signatureBase64, "The update feed signature");
            verify_p256_signature(payload, signature, key);
            const std::string payloadText(
                reinterpret_cast<const char*>(payload.data()), payload.size());
            return { std::move(payload), detail::parse_json(payloadText) };
        }

        [[nodiscard]] LauncherFeed parse_launcher_payload(const detail::Json& root)
        {
            detail::require_exact_object(root,
                { "schemaVersion", "productId", "channel", "releaseSequence",
                    "version", "sourceCommit", "artifact" },
                "The authenticated launcher update payload");
            LauncherFeed feed;
            feed.schemaVersion = static_cast<int>(detail::integer_value(
                detail::member(root, "schemaVersion"), "Feed schema version"));
            feed.productId = detail::string_value(
                detail::member(root, "productId"), "Product ID");
            feed.channel = detail::string_value(
                detail::member(root, "channel"), "Channel");
            feed.releaseSequence = detail::integer_value(
                detail::member(root, "releaseSequence"), "Release sequence");
            feed.version = detail::string_value(
                detail::member(root, "version"), "Launcher version");
            feed.sourceCommit = detail::string_value(
                detail::member(root, "sourceCommit"), "Source commit");
            const detail::Json& artifact = detail::member(root, "artifact");
            detail::require_exact_object(artifact,
                { "name", "size", "sha256" }, "The launcher artifact");
            feed.artifact.name = detail::string_value(
                detail::member(artifact, "name"), "Artifact name");
            feed.artifact.size = detail::unsigned_value(
                detail::member(artifact, "size"), "Artifact size");
            feed.artifact.sha256 = detail::string_value(
                detail::member(artifact, "sha256"), "Artifact SHA-256");
            return feed;
        }

        [[nodiscard]] RendererFeed parse_renderer_payload(const detail::Json& root)
        {
            detail::require_exact_object(root,
                { "schemaVersion", "productId", "channel", "releaseSequence",
                    "sourceCommit", "settingsHash", "engineVersion", "artifact" },
                "The authenticated renderer update payload");
            RendererFeed feed;
            feed.schemaVersion = static_cast<int>(detail::integer_value(
                detail::member(root, "schemaVersion"), "Feed schema version"));
            feed.productId = detail::string_value(
                detail::member(root, "productId"), "Product ID");
            feed.channel = detail::string_value(
                detail::member(root, "channel"), "Channel");
            feed.releaseSequence = detail::integer_value(
                detail::member(root, "releaseSequence"), "Release sequence");
            feed.sourceCommit = detail::string_value(
                detail::member(root, "sourceCommit"), "Source commit");
            feed.settingsHash = detail::string_value(
                detail::member(root, "settingsHash"), "Settings hash");
            feed.engineVersion = detail::string_value(
                detail::member(root, "engineVersion"), "Engine version");
            const detail::Json& artifact = detail::member(root, "artifact");
            detail::require_exact_object(artifact,
                { "name", "size", "sha256" }, "The renderer artifact");
            feed.artifact.name = detail::string_value(
                detail::member(artifact, "name"), "Artifact name");
            feed.artifact.size = detail::unsigned_value(
                detail::member(artifact, "size"), "Artifact size");
            feed.artifact.sha256 = detail::string_value(
                detail::member(artifact, "sha256"), "Artifact SHA-256");
            return feed;
        }

        void verify_zero_certificate_table(const fs::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw Error("The launcher executable could not be inspected.");
            auto read16 = [&input]()
            {
                std::array<unsigned char, 2> value{};
                input.read(reinterpret_cast<char*>(value.data()), value.size());
                if (!input)
                    throw Error("The launcher executable was truncated.");
                return static_cast<std::uint16_t>(value[0] | value[1] << 8u);
            };
            auto read32 = [&input]()
            {
                std::array<unsigned char, 4> value{};
                input.read(reinterpret_cast<char*>(value.data()), value.size());
                if (!input)
                    throw Error("The launcher executable was truncated.");
                return static_cast<std::uint32_t>(value[0] | value[1] << 8u |
                    value[2] << 16u | value[3] << 24u);
            };
            if (read16() != IMAGE_DOS_SIGNATURE)
                throw Error("The launcher executable had an invalid DOS header.");
            input.seekg(0x3c);
            const std::uint32_t pe = read32();
            input.seekg(pe);
            if (read32() != IMAGE_NT_SIGNATURE)
                throw Error("The launcher executable had an invalid PE header.");
            input.seekg(pe + 20u);
            const std::uint16_t optionalSize = read16();
            const std::uint64_t optionalStart = pe + 24ull;
            input.seekg(static_cast<std::streamoff>(optionalStart));
            const std::uint16_t magic = read16();
            const std::uint32_t directoryCountOffset =
                magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? 0x6cu :
                magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ? 0x5cu : 0u;
            const std::uint32_t directoriesOffset =
                magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? 0x70u :
                magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ? 0x60u : 0u;
            if (directoryCountOffset == 0 ||
                optionalSize < directoriesOffset + 5u * 8u)
            {
                throw Error("The launcher executable had an invalid optional header.");
            }
            input.seekg(static_cast<std::streamoff>(
                optionalStart + directoryCountOffset));
            const std::uint32_t count = read32();
            if (count < 5u || count > IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
                directoriesOffset + count * 8ull > optionalSize)
            {
                throw Error("The launcher executable had invalid data directories.");
            }
            input.seekg(static_cast<std::streamoff>(
                optionalStart + directoriesOffset + 4u * 8u));
            if (read32() != 0 || read32() != 0)
            {
                throw Error("An unsigned launcher contained Certificate Table metadata.");
            }
        }
    }

    void validate_launcher_feed(const LauncherFeed& feed)
    {
        if (feed.schemaVersion != 2 || feed.productId != ProductId ||
            feed.channel != "stable" || feed.releaseSequence < 1 ||
            feed.releaseSequence > MaximumReleaseSequence ||
            !is_stable_version(feed.version) ||
            !is_lower_hex(feed.sourceCommit, 40u) ||
            feed.artifact.name != "uvsr-launcher.exe" ||
            feed.artifact.size == 0 ||
            feed.artifact.size > MaximumLauncherBytes ||
            !is_lower_hex(feed.artifact.sha256, 64u))
        {
            throw Error("The launcher update feed did not match its strict contract.");
        }
    }

    void validate_renderer_feed(const RendererFeed& feed)
    {
        if (feed.schemaVersion != 1 || feed.productId != ProductId ||
            feed.channel != "stable" || feed.releaseSequence < 1 ||
            feed.releaseSequence > MaximumReleaseSequence ||
            !is_lower_hex(feed.sourceCommit, 40u) ||
            !is_lower_hex(feed.settingsHash, 32u) ||
            !is_engine_version(feed.engineVersion) ||
            feed.artifact.name != "uvsr-renderer-windows-11-x64.zip" ||
            feed.artifact.size == 0 ||
            feed.artifact.size > MaximumRendererPackageBytes ||
            !is_lower_hex(feed.artifact.sha256, 64u))
        {
            throw Error("The renderer update feed did not match its strict contract.");
        }
    }

    LauncherFeed verify_launcher_feed(
        std::span<const std::byte> envelope,
        std::span<const std::byte> publicKey,
        std::string_view keyId)
    {
        const auto verified = verify_envelope(
            envelope, 2, publicKey, keyId, "launcher");
        LauncherFeed feed = parse_launcher_payload(verified.json);
        validate_launcher_feed(feed);
        return feed;
    }

    RendererFeed verify_renderer_feed(
        std::span<const std::byte> envelope,
        std::span<const std::byte> publicKey,
        std::string_view keyId)
    {
        const auto verified = verify_envelope(
            envelope, 1, publicKey, keyId, "renderer");
        RendererFeed feed = parse_renderer_payload(verified.json);
        validate_renderer_feed(feed);
        return feed;
    }

    std::wstring launcher_artifact_url(const LauncherFeed& feed)
    {
        validate_launcher_feed(feed);
        return L"https://github.com/brockliddicoat/uvsr/releases/download/uvsr-launcher-v" +
            widen(feed.version) + L"/uvsr-launcher.exe";
    }

    std::wstring renderer_artifact_url(const RendererFeed& feed)
    {
        validate_renderer_feed(feed);
        return L"https://github.com/brockliddicoat/uvsr/releases/download/uvsr-engine-r" +
            std::to_wstring(feed.releaseSequence) +
            L"/uvsr-renderer-windows-11-x64.zip";
    }

    ComponentUpdateState classify_launcher_update(
        std::int64_t currentSequence,
        std::string_view currentVersion,
        std::string_view currentHash,
        bool recordedStateHealthy,
        const LauncherFeed& available)
    {
        validate_launcher_feed(available);
        if (currentSequence < 1 || currentSequence > MaximumReleaseSequence ||
            !is_stable_version(currentVersion) || !is_lower_hex(currentHash, 64u))
        {
            throw Error("The current launcher release identity was invalid.");
        }
        if (available.releaseSequence < currentSequence)
            return ComponentUpdateState::Current;
        if (available.releaseSequence > currentSequence)
            return ComponentUpdateState::UpdateAvailable;
        if (available.version != currentVersion ||
            available.artifact.sha256 != currentHash)
        {
            throw Error("The launcher update feed reused a release sequence with different files.");
        }
        return recordedStateHealthy
            ? ComponentUpdateState::Current
            : ComponentUpdateState::RepairNeeded;
    }

    ComponentUpdateState classify_renderer_update(
        const InstallSnapshot& current,
        const RendererFeed& available)
    {
        validate_renderer_feed(available);
        if (current.damaged)
            return ComponentUpdateState::RepairNeeded;
        if (!current.installed || !current.state)
            return ComponentUpdateState::NotInstalled;
        const InstallState& state = *current.state;
        detail::validate_install_state(state, state.installationId);
        if (available.releaseSequence < state.releaseSequence)
            return ComponentUpdateState::Current;
        if (available.releaseSequence > state.releaseSequence)
            return ComponentUpdateState::UpdateAvailable;
        if (available.sourceCommit != state.commit ||
            available.settingsHash != state.settingsHash ||
            available.engineVersion != state.engineVersion ||
            available.artifact.sha256 != state.artifactSha256)
        {
            throw Error("The renderer feed reused a release sequence with different files or settings.");
        }
        return ComponentUpdateState::Current;
    }

    void validate_pe_x64(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw Error("The launcher file could not be inspected safely.");
        IMAGE_DOS_HEADER dos{};
        input.read(reinterpret_cast<char*>(&dos), sizeof(dos));
        if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0x40)
            throw Error("The launcher file was not a Windows program.");
        input.seekg(dos.e_lfanew);
        DWORD signature = 0;
        IMAGE_FILE_HEADER fileHeader{};
        input.read(reinterpret_cast<char*>(&signature), sizeof(signature));
        input.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        if (!input || signature != IMAGE_NT_SIGNATURE ||
            fileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        {
            throw Error("The launcher file was not an x64 Windows program.");
        }
    }

    void validate_launcher_metadata(
        const fs::path& path,
        std::string_view version,
        std::optional<std::string_view> sourceCommit)
    {
        const std::string productName = file_version_string(path, L"ProductName");
        const std::string fileVersion = file_version_string(path, L"FileVersion");
        const std::string productVersion = file_version_string(path, L"ProductVersion");
        const std::string expectedFile = std::string(version) + ".0";
        const std::string expectedProduct = sourceCommit
            ? std::string(version) + "+" + std::string(*sourceCommit)
            : std::string(version);
        const bool currentProductAccepted = !sourceCommit &&
            (productVersion == expectedProduct ||
                productVersion.starts_with(expectedProduct + "+"));
        if (productName != "UVSR Launcher" || fileVersion != expectedFile ||
            (!currentProductAccepted && productVersion != expectedProduct))
        {
            throw Error("The launcher file had unexpected product metadata.");
        }
    }

    void verify_launcher_authenticode_policy(const fs::path& path)
    {
        WINTRUST_FILE_INFO fileInfo{};
        fileInfo.cbStruct = sizeof(fileInfo);
        fileInfo.pcwszFilePath = path.c_str();
        WINTRUST_DATA trust{};
        trust.cbStruct = sizeof(trust);
        trust.dwUIChoice = WTD_UI_NONE;
        trust.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
        trust.dwUnionChoice = WTD_CHOICE_FILE;
        trust.pFile = &fileInfo;
        trust.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
        GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        const LONG result = WinVerifyTrust(nullptr, &action, &trust);
        if (result == ERROR_SUCCESS)
            return;
        if (result != TRUST_E_NOSIGNATURE)
        {
            char message[128]{};
            sprintf_s(message,
                "The launcher update had invalid Authenticode status 0x%08lx.",
                static_cast<unsigned long>(result));
            throw Error(message);
        }
        verify_zero_certificate_table(path);
    }
}

namespace uvsr::launcher
{
    namespace
    {
        struct ProcessQuery
        {
            ProcessState state = ProcessState::Unverifiable;
            fs::path path;
            std::uint64_t creationTime = 0;
            Handle process;
        };

        [[nodiscard]] ProcessQuery query_process(
            DWORD processId,
            DWORD access = PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE)
        {
            Handle process(OpenProcess(access, FALSE, processId));
            if (!process)
            {
                return { GetLastError() == ERROR_INVALID_PARAMETER
                    ? ProcessState::NotRunning
                    : ProcessState::Unverifiable };
            }
            const DWORD wait = WaitForSingleObject(process.get(), 0);
            if (wait == WAIT_OBJECT_0)
                return { ProcessState::NotRunning };
            if (wait != WAIT_TIMEOUT)
                return { ProcessState::Unverifiable };
            std::wstring path(32768u, L'\0');
            DWORD characters = static_cast<DWORD>(path.size());
            FILETIME created{}, exited{}, kernel{}, user{};
            if (!QueryFullProcessImageNameW(process.get(), 0,
                    path.data(), &characters) ||
                !GetProcessTimes(process.get(), &created, &exited, &kernel, &user))
            {
                return { ProcessState::Unverifiable };
            }
            path.resize(characters);
            ProcessQuery result;
            result.state = ProcessState::Running;
            result.path = full_path(path);
            result.creationTime = file_time_value(created);
            result.process = std::move(process);
            return result;
        }

        [[nodiscard]] bool exact_match(
            const ExactProcessIdentity& expected,
            const ProcessQuery& actual)
        {
            return actual.state == ProcessState::Running &&
                expected.creationTime == actual.creationTime &&
                detail::path_equal(expected.executablePath, actual.path);
        }

        struct CloseWindowContext
        {
            DWORD processId = 0;
            bool sent = false;
        };

        BOOL CALLBACK close_window(HWND window, LPARAM parameter)
        {
            auto& context = *reinterpret_cast<CloseWindowContext*>(parameter);
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            if (processId == context.processId && IsWindowVisible(window) &&
                GetWindow(window, GW_OWNER) == nullptr)
            {
                context.sent = PostMessageW(window, WM_CLOSE, 0, 0) != FALSE ||
                    context.sent;
            }
            return TRUE;
        }
    }

    ExactProcessInspection inspect_managed_processes(
        std::wstring_view executableName,
        const fs::path& root,
        bool excludeCurrent)
    {
        ExactProcessInspection result;
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snapshot)
        {
            result.state = ProcessState::Unverifiable;
            return result;
        }
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (!Process32FirstW(snapshot.get(), &entry))
        {
            result.state = GetLastError() == ERROR_NO_MORE_FILES
                ? ProcessState::NotRunning
                : ProcessState::Unverifiable;
            return result;
        }
        bool unverifiable = false;
        do
        {
            if (excludeCurrent && entry.th32ProcessID == GetCurrentProcessId())
                continue;
            if (!ordinal_equal(entry.szExeFile, executableName))
                continue;
            ProcessQuery query = query_process(entry.th32ProcessID);
            if (query.state == ProcessState::Unverifiable)
            {
                unverifiable = true;
                continue;
            }
            if (query.state != ProcessState::Running)
                continue;
            try
            {
                if (is_strict_descendant(query.path, root))
                {
                    result.matches.push_back({ entry.th32ProcessID,
                        query.creationTime, query.path });
                }
            }
            catch (const Error&)
            {
                unverifiable = true;
            }
        } while (Process32NextW(snapshot.get(), &entry));
        result.state = !result.matches.empty()
            ? ProcessState::Running
            : unverifiable ? ProcessState::Unverifiable : ProcessState::NotRunning;
        return result;
    }

    ExactProcessInspection inspect_processes_by_executable(
        const fs::path& executable)
    {
        const fs::path expected = full_path(executable);
        ExactProcessInspection candidates = inspect_managed_processes(
            expected.filename().native(), expected.parent_path());
        std::erase_if(candidates.matches,
            [&expected](const ExactProcessIdentity& candidate)
            {
                return !detail::path_equal(candidate.executablePath, expected);
            });
        if (!candidates.matches.empty())
            candidates.state = ProcessState::Running;
        else if (candidates.state == ProcessState::Running)
            candidates.state = ProcessState::NotRunning;
        return candidates;
    }

    ProcessState inspect_exact_process(const ExactProcessIdentity& identity)
    {
        ProcessQuery query = query_process(identity.processId);
        if (query.state != ProcessState::Running)
            return query.state;
        return exact_match(identity, query)
            ? ProcessState::Running
            : ProcessState::NotRunning;
    }

    bool close_exact_process(const ExactProcessIdentity& identity)
    {
        ProcessQuery query = query_process(identity.processId);
        if (!exact_match(identity, query))
            return false;
        CloseWindowContext context{ identity.processId, false };
        EnumWindows(close_window, reinterpret_cast<LPARAM>(&context));
        return context.sent;
    }

    bool terminate_exact_process(const ExactProcessIdentity& identity)
    {
        ProcessQuery query = query_process(identity.processId,
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE);
        return exact_match(identity, query) &&
            TerminateProcess(query.process.get(), 1) != FALSE;
    }
}

namespace uvsr::launcher::detail
{
    namespace
    {
        [[nodiscard]] std::wstring operation_mutex_name()
        {
            Handle token;
            HANDLE raw = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
                throw_last_error("Windows could not identify the current user");
            token.reset(raw);
            DWORD bytes = 0;
            GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
                throw_last_error("Windows could not inspect the current user");
            std::vector<std::byte> data(bytes);
            if (!GetTokenInformation(token.get(), TokenUser,
                    data.data(), bytes, &bytes))
            {
                throw_last_error("Windows could not inspect the current user");
            }
            const auto* user = reinterpret_cast<const TOKEN_USER*>(data.data());
            PWSTR rawSid = nullptr;
            if (!ConvertSidToStringSidW(user->User.Sid, &rawSid) || rawSid == nullptr)
                throw_last_error("Windows could not format the current user identity");
            std::wstring sid(rawSid);
            LocalFree(rawSid);
            return L"Global\\UVSR.Installer." + widen(ProductId) + L"." + sid;
        }

        [[nodiscard]] std::optional<OwnerMarker> inspect_root(
            const fs::path& root,
            const fs::path& marker,
            std::string_view description)
        {
            if (!fs::exists(root))
                return std::nullopt;
            reject_reparse_chain(root, description);
            if (fs::exists(marker))
                return parse_owner(read_text(marker, MaximumStateBytes));
            if (!fs::is_empty(root))
            {
                throw Error("The existing directory '" + narrow(root.native()) +
                    "' was not owned by UVSR Launcher. It was preserved.");
            }
            return std::nullopt;
        }

        void ensure_owned_root(
            const fs::path& root,
            const fs::path& markerPath,
            const OwnerMarker& marker)
        {
            reject_reparse_chain(root, "UVSR installer owned directory");
            if (fs::exists(root))
            {
                if (fs::exists(markerPath))
                {
                    const OwnerMarker current = parse_owner(
                        read_text(markerPath, MaximumStateBytes));
                    if (!guid_equal(current.installationId, marker.installationId))
                        throw Error("UVSR ownership records did not match.");
                    return;
                }
                if (!fs::is_empty(root))
                    throw Error("An existing UVSR directory was not owned and was preserved.");
            }
            else
                fs::create_directories(root);
            write_atomic(markerPath, serialize_owner(marker));
        }

        [[nodiscard]] std::string sanitize_log(std::string_view message)
        {
            std::string clean(message);
            std::replace(clean.begin(), clean.end(), '\r', ' ');
            std::replace(clean.begin(), clean.end(), '\n', ' ');
            if (clean.size() > 4000u)
                clean.resize(4000u);
            return clean;
        }
    }

    OperationLock::OperationLock()
    {
        m_Handle = CreateMutexW(nullptr, FALSE, operation_mutex_name().c_str());
        if (m_Handle == nullptr)
            throw_last_error("Windows could not create the UVSR operation lock");
        const DWORD result = WaitForSingleObject(m_Handle, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
        {
            CloseHandle(m_Handle);
            m_Handle = nullptr;
            throw Error("Another UVSR install, update, repair, or uninstall is already running.");
        }
    }

    OperationLock::~OperationLock()
    {
        if (m_Handle != nullptr)
        {
            ReleaseMutex(m_Handle);
            CloseHandle(m_Handle);
        }
    }

    InstallLog::InstallLog(const fs::path& logsDirectory, LogCallback observer)
        : m_Observer(std::move(observer))
    {
        reject_reparse_chain(logsDirectory, "installer log directory");
        fs::create_directories(logsDirectory);
        std::vector<fs::directory_entry> logs;
        for (const auto& entry : fs::directory_iterator(logsDirectory))
        {
            if (entry.is_regular_file() &&
                entry.path().filename().native().starts_with(L"uvsr-launcher-") &&
                entry.path().extension() == L".log")
            {
                logs.push_back(entry);
            }
        }
        std::sort(logs.begin(), logs.end(), [](const auto& left, const auto& right)
        {
            return left.last_write_time() > right.last_write_time();
        });
        for (std::size_t index = 4u; index < logs.size(); ++index)
        {
            std::error_code ignored;
            fs::remove(logs[index], ignored);
        }
        SYSTEMTIME time{};
        GetSystemTime(&time);
        wchar_t name[128]{};
        swprintf_s(name, L"uvsr-launcher-%04u%02u%02u-%02u%02u%02u-%s.log",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
            time.wSecond, widen(guid_string(new_guid())).c_str());
        m_Path = logsDirectory / name;
    }

    void InstallLog::write(std::string_view message)
    {
        std::lock_guard lock(m_Mutex);
        std::string clean = sanitize_log(message);
        if (!m_Truncated && fs::exists(m_Path) &&
            fs::file_size(m_Path) >= 8ull * 1024ull * 1024ull)
        {
            clean = "UVSR Launcher details reached the 8 MB safety limit; additional output was omitted.";
            m_Truncated = true;
        }
        if (m_Truncated && clean !=
            "UVSR Launcher details reached the 8 MB safety limit; additional output was omitted.")
        {
            return;
        }
        reject_reparse_chain(m_Path, "installer log file");
        std::ofstream output(m_Path, std::ios::binary | std::ios::app);
        if (!output)
            throw Error("Windows could not open the UVSR Launcher details log.");
        output << now_iso8601() << ' ' << clean << "\r\n";
        output.flush();
        if (!output)
            throw Error("Windows could not write the UVSR Launcher details log.");
        if (m_Observer)
            m_Observer(clean);
    }

    const fs::path& InstallLog::path() const noexcept
    {
        return m_Path;
    }

    Ownership::Ownership(InstallerPaths paths) : m_Paths(std::move(paths)) {}

    std::optional<OwnerMarker> Ownership::inspect() const
    {
        const auto program = inspect_root(m_Paths.programRoot,
            m_Paths.program_marker(), "UVSR program directory");
        const auto state = inspect_root(m_Paths.stateRoot,
            m_Paths.state_marker(), "UVSR installer state directory");
        if (!program && !state)
            return std::nullopt;
        const OwnerMarker marker = program ? *program : *state;
        validate_owner(marker);
        if (program && state &&
            !guid_equal(program->installationId, state->installationId))
        {
            throw Error("UVSR ownership records did not match. No files were changed.");
        }
        return marker;
    }

    OwnerMarker Ownership::ensure_roots() const
    {
        OwnerMarker marker;
        if (const auto existing = inspect())
            marker = *existing;
        else
        {
            marker.schemaVersion = SchemaVersion;
            marker.productId = ProductId;
            marker.installationId = new_guid();
        }
        ensure_owned_root(m_Paths.programRoot, m_Paths.program_marker(), marker);
        ensure_owned_root(m_Paths.stateRoot, m_Paths.state_marker(), marker);
        fs::create_directories(m_Paths.logs_directory());
        return marker;
    }

    void Ownership::validate_both(const GUID& installationId) const
    {
        const auto marker = inspect();
        if (!marker || !guid_equal(marker->installationId, installationId) ||
            !fs::exists(m_Paths.program_marker()) ||
            !fs::exists(m_Paths.state_marker()))
        {
            throw Error("UVSR ownership could not be proven. No files were changed.");
        }
    }
}

namespace uvsr::launcher
{
    std::optional<OwnerMarker> inspect_owner(const InstallerPaths& paths)
    {
        return detail::Ownership(paths).inspect();
    }
}

namespace uvsr::launcher::detail
{
    namespace
    {
        void close_registry(HKEY key)
        {
            (void)RegCloseKey(key);
        }

        template<typename T>
        void release_com(T* value)
        {
            value->Release();
        }

        using Registry = Scoped<HKEY, close_registry>;

        template<typename T>
        using Com = Scoped<T*, release_com<T>>;

        constexpr std::wstring_view UninstallParent =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
        constexpr std::wstring_view UninstallName = L"UVSR";

        [[nodiscard]] Registry open_registry(
            HKEY root,
            std::wstring_view path,
            REGSAM access,
            bool required = false)
        {
            HKEY raw = nullptr;
            const LSTATUS status = RegOpenKeyExW(root,
                std::wstring(path).c_str(), 0, access, &raw);
            if (status == ERROR_FILE_NOT_FOUND && !required)
                return {};
            if (status != ERROR_SUCCESS)
                throw Error("Windows could not open the per user UVSR registry entry: " +
                    win32_message(status));
            return Registry(raw);
        }

        [[nodiscard]] Registry create_registry(
            HKEY root,
            std::wstring_view path)
        {
            HKEY raw = nullptr;
            DWORD disposition = 0;
            const LSTATUS status = RegCreateKeyExW(root,
                std::wstring(path).c_str(), 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
                nullptr, &raw, &disposition);
            if (status != ERROR_SUCCESS)
                throw Error("Windows could not create the per user UVSR registry entry: " +
                    win32_message(status));
            return Registry(raw);
        }

        [[nodiscard]] std::wstring registry_string(
            HKEY key,
            std::wstring_view name)
        {
            DWORD type = 0;
            DWORD bytes = 0;
            LSTATUS status = RegQueryValueExW(key, std::wstring(name).c_str(),
                nullptr, &type, nullptr, &bytes);
            if (status == ERROR_FILE_NOT_FOUND)
                return {};
            if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) ||
                bytes > 64u * 1024u)
            {
                return {};
            }
            std::wstring value(bytes / sizeof(wchar_t), L'\0');
            status = RegQueryValueExW(key, std::wstring(name).c_str(),
                nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
            if (status != ERROR_SUCCESS)
                return {};
            while (!value.empty() && value.back() == L'\0')
                value.pop_back();
            return value;
        }

        void set_registry_string(
            HKEY key,
            std::wstring_view name,
            std::wstring_view value)
        {
            const DWORD bytes = static_cast<DWORD>(
                (value.size() + 1u) * sizeof(wchar_t));
            const LSTATUS status = RegSetValueExW(key,
                std::wstring(name).c_str(), 0, REG_SZ,
                reinterpret_cast<const BYTE*>(value.data()), bytes);
            if (status != ERROR_SUCCESS)
                throw Error("Windows could not write the UVSR Apps & Features entry: " +
                    win32_message(status));
        }

        void set_registry_dword(
            HKEY key,
            std::wstring_view name,
            DWORD value)
        {
            const LSTATUS status = RegSetValueExW(key,
                std::wstring(name).c_str(), 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&value), sizeof(value));
            if (status != ERROR_SUCCESS)
                throw Error("Windows could not write the UVSR Apps & Features entry: " +
                    win32_message(status));
        }

        [[nodiscard]] bool registry_owned(HKEY key, const GUID& installationId)
        {
            return ordinal_equal(registry_string(key, L"UVSRProductId"),
                       widen(ProductId)) &&
                ordinal_equal(registry_string(key, L"UVSRInstallId"),
                    widen(guid_string(installationId)));
        }

        [[nodiscard]] std::wstring compact_guid(const GUID& value)
        {
            std::wstring result = widen(guid_string(value));
            std::erase(result, L'-');
            return result;
        }

        [[nodiscard]] std::wstring staging_registry_name(const GUID& value)
        {
            return L"UVSR Installer " + compact_guid(value) + L".staging";
        }

        void delete_registry_tree(HKEY parent, std::wstring_view name)
        {
            const LSTATUS status = RegDeleteTreeW(
                parent, std::wstring(name).c_str());
            if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
                throw Error("Windows could not remove an owned UVSR registry entry: " +
                    win32_message(status));
        }

        void write_registry_values(
            HKEY key,
            const InstallerPaths& paths,
            const GUID& installationId,
            const std::optional<InstallState>& renderer,
            const LauncherState& launcher,
            const fs::path& launcherPath)
        {
            set_registry_string(key, L"UVSRProductId", widen(ProductId));
            set_registry_string(key, L"UVSRInstallId",
                widen(guid_string(installationId)));
            set_registry_string(key, L"DisplayName", L"UVSR Launcher");
            set_registry_string(key, L"DisplayVersion", widen(launcher.version));
            set_registry_string(key, L"Publisher", L"UVSR");
            set_registry_string(key, L"InstallLocation", paths.programRoot.native());
            set_registry_string(key, L"DisplayIcon", launcherPath.native());
            set_registry_string(key, L"UninstallString",
                L"\"" + launcherPath.native() + L"\" --uninstall");
            set_registry_string(key, L"ModifyPath",
                L"\"" + launcherPath.native() + L"\"");
            set_registry_dword(key, L"NoRepair", 1);
            std::uint64_t bytes = directory_bytes(
                paths.launcher_version_root(launcher.executableSha256));
            if (renderer)
                bytes += directory_bytes(paths.version_root(renderer->activeVersionId));
            set_registry_dword(key, L"EstimatedSize", static_cast<DWORD>(
                std::min<std::uint64_t>(
                    std::numeric_limits<DWORD>::max(), bytes / 1024u)));
            RegFlushKey(key);
        }

        void promote_registry(
            const InstallerPaths& paths,
            const GUID& installationId,
            const std::optional<InstallState>& renderer,
            const LauncherState& launcher,
            const fs::path& launcherPath,
            const GUID& transactionId)
        {
            Registry parent = create_registry(HKEY_CURRENT_USER, UninstallParent);
            const std::wstring staging = staging_registry_name(transactionId);
            delete_registry_tree(parent.get(), staging);
            {
                Registry staged = create_registry(parent.get(), staging);
                write_registry_values(staged.get(), paths, installationId,
                    renderer, launcher, launcherPath);
            }

            Registry current = open_registry(parent.get(), UninstallName,
                KEY_READ | KEY_WRITE);
            if (current && !registry_owned(current.get(), installationId))
            {
                delete_registry_tree(parent.get(), staging);
                throw Error("An unrelated Apps & Features entry already used the UVSR name. It was preserved.");
            }
            current.reset();

            const std::wstring backup = L"UVSR.backup-" + compact_guid(transactionId);
            delete_registry_tree(parent.get(), backup);
            bool movedCurrent = false;
            current = open_registry(parent.get(), UninstallName, KEY_READ);
            if (current)
            {
                current.reset();
                const LSTATUS renamed = RegRenameKey(parent.get(),
                    UninstallName.data(), backup.c_str());
                if (renamed != ERROR_SUCCESS)
                {
                    delete_registry_tree(parent.get(), staging);
                    throw Error("Windows could not stage the UVSR Apps & Features update: " +
                        win32_message(renamed));
                }
                movedCurrent = true;
            }
            const LSTATUS promoted = RegRenameKey(
                parent.get(), staging.c_str(), UninstallName.data());
            if (promoted != ERROR_SUCCESS)
            {
                if (movedCurrent)
                    (void)RegRenameKey(parent.get(), backup.c_str(), UninstallName.data());
                delete_registry_tree(parent.get(), staging);
                throw Error("Windows could not activate the UVSR Apps & Features entry: " +
                    win32_message(promoted));
            }
            if (movedCurrent)
                delete_registry_tree(parent.get(), backup);
        }

        [[nodiscard]] std::optional<std::string> capture_file(const fs::path& path)
        {
            if (!fs::exists(path))
                return std::nullopt;
            const auto bytes = read_file(path, 1024u * 1024u, true);
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }

        void restore_file(
            const fs::path& path,
            const std::optional<std::string>& contents)
        {
            if (!contents)
            {
                DeleteFileW(path.c_str());
                return;
            }
            write_atomic(path, *contents);
        }

        [[nodiscard]] bool launcher_target_in_layout(
            const InstallerPaths& paths,
            const fs::path& target)
        {
            try
            {
                const fs::path full = full_path(target);
                if (!ordinal_equal(full.filename().native(), LauncherExecutableName))
                    return false;
                const fs::path versionRoot = full.parent_path();
                const std::string hash = narrow(versionRoot.filename().native());
                return is_lower_hex(hash, 64u) &&
                    path_equal(versionRoot.parent_path(),
                        paths.launcher_versions_directory()) &&
                    path_equal(full, paths.launcher_executable(hash));
            }
            catch (const std::exception&)
            {
                return false;
            }
        }

        void remove_shortcut_if_owned(
            const InstallerPaths& paths,
            const fs::path& path,
            const GUID& installationId,
            InstallLog& log)
        {
            if (!fs::exists(path))
                return;
            try
            {
                validate_shell_path(paths, path, "managed shortcut path");
                const ShortcutInfo shortcut = ShellIntegration::read_shortcut(path);
                ShellIntegration shell(paths);
                if (!shell.shortcut_owned(shortcut, installationId))
                {
                    log.write("Preserved a modified or unrelated shortcut: " +
                        narrow(path.native()));
                    return;
                }
                if (!DeleteFileW(path.c_str()))
                    throw_last_error("Windows could not remove an owned UVSR shortcut");
            }
            catch (const std::exception& error)
            {
                log.write("Preserved a shortcut that could not be inspected safely: " +
                    narrow(path.native()) + " (" + error.what() + ")");
            }
        }
    }

    ShellIntegration::ShellIntegration(InstallerPaths paths)
        : m_Paths(std::move(paths)) {}

    void ShellIntegration::validate_can_apply(
        const GUID& installationId,
        bool desktopShortcut,
        const std::optional<GUID>&) const
    {
        Registry key = open_registry(HKEY_CURRENT_USER,
            std::wstring(UninstallParent) + L"\\" + std::wstring(UninstallName),
            KEY_READ);
        if (key && !registry_owned(key.get(), installationId))
            throw Error("An unrelated Apps & Features entry already used the UVSR name. It was preserved.");
        validate_shell_path(m_Paths, m_Paths.start_menu_directory(),
            "UVSR Start menu directory");
        const auto validateShortcut = [&](const fs::path& path,
            std::string_view message)
        {
            if (!fs::exists(path))
                return;
            validate_shell_path(m_Paths, path, "shortcut collision path");
            if (!shortcut_owned(read_shortcut(path), installationId))
                throw Error(std::string(message));
        };
        validateShortcut(m_Paths.start_menu_shortcut(),
            "An unrelated Start menu shortcut already used the UVSR Launcher name.");
        if (desktopShortcut)
            validateShortcut(m_Paths.desktop_shortcut(),
                "An unrelated desktop shortcut named UVSR Launcher already existed. It was preserved.");
    }

    void ShellIntegration::apply(
        const GUID& installationId,
        const std::optional<InstallState>& renderer,
        const LauncherState& launcher,
        InstallLog& log,
        const std::optional<GUID>& transactionId) const
    {
        validate_launcher_state(launcher, installationId);
        if (renderer)
            validate_install_state(*renderer, installationId);
        validate_can_apply(installationId, launcher.desktopShortcut, transactionId);
        const fs::path launcherPath = m_Paths.launcher_executable(
            launcher.executableSha256);
        if (!fs::is_regular_file(launcherPath))
            throw Error("The installed UVSR Launcher was incomplete.");
        if (renderer && !fs::is_regular_file(
                m_Paths.version_executable(renderer->activeVersionId)))
        {
            throw Error("The installed UVSR program was incomplete.");
        }

        const auto startSnapshot = capture_file(m_Paths.start_menu_shortcut());
        const auto desktopSnapshot = capture_file(m_Paths.desktop_shortcut());
        try
        {
            create_shortcut(m_Paths.start_menu_shortcut(), launcherPath, L"",
                launcherPath.parent_path(),
                L"Install, launch, update, or remove UVSR");
            if (launcher.desktopShortcut)
            {
                create_shortcut(m_Paths.desktop_shortcut(), launcherPath, L"",
                    launcherPath.parent_path(), L"Open UVSR Launcher");
            }
            else
            {
                remove_shortcut_if_owned(m_Paths, m_Paths.desktop_shortcut(),
                    installationId, log);
            }
            const GUID transaction = transactionId.value_or(new_guid());
            promote_registry(m_Paths, installationId, renderer,
                launcher, launcherPath, transaction);
        }
        catch (...)
        {
            try
            {
                restore_file(m_Paths.start_menu_shortcut(), startSnapshot);
                restore_file(m_Paths.desktop_shortcut(), desktopSnapshot);
            }
            catch (...)
            {
                throw Error("Windows shell integration failed and its shortcuts could not be restored completely. Reopen UVSR Launcher to finish recovery.");
            }
            throw;
        }
        log.write("Updated the per user Start menu, desktop shortcut preference, and Apps & Features entry.");
    }

    void ShellIntegration::remove(const GUID& installationId, InstallLog& log) const
    {
        Registry parent = open_registry(HKEY_CURRENT_USER,
            UninstallParent, KEY_READ | KEY_WRITE);
        if (parent)
        {
            Registry current = open_registry(parent.get(), UninstallName, KEY_READ);
            if (current && !registry_owned(current.get(), installationId))
                throw Error("The UVSR Apps & Features entry was not owned by this installation. It was preserved.");
            current.reset();
            if (parent)
                delete_registry_tree(parent.get(), UninstallName);
        }
        remove_shortcut_if_owned(m_Paths, m_Paths.desktop_shortcut(),
            installationId, log);
        remove_shortcut_if_owned(m_Paths, m_Paths.start_menu_shortcut(),
            installationId, log);
        if (fs::exists(m_Paths.start_menu_directory()) &&
            fs::is_empty(m_Paths.start_menu_directory()))
        {
            fs::remove(m_Paths.start_menu_directory());
        }
        log.write("Removed UVSR's owned per user shortcuts and Apps & Features entry.");
    }

    void ShellIntegration::launch(const InstallState& state) const
    {
        validate_install_state(state, state.installationId);
        const fs::path executable = m_Paths.version_executable(state.activeVersionId);
        if (!fs::is_regular_file(executable) ||
            sha256(executable) != state.executableSha256)
        {
            throw Error("The installed UVSR executable was missing or changed. Choose Install to repair it.");
        }
        const fs::path working = m_Paths.version_root(state.activeVersionId);
        SHELLEXECUTEINFOW start{};
        start.cbSize = sizeof(start);
        start.fMask = SEE_MASK_NOCLOSEPROCESS;
        start.lpFile = executable.c_str();
        start.lpDirectory = working.c_str();
        start.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&start))
            throw_last_error("Windows could not open UVSR");
        if (start.hProcess != nullptr)
            CloseHandle(start.hProcess);
    }

    bool ShellIntegration::shortcut_owned(
        const ShortcutInfo& shortcut,
        const GUID& installationId) const
    {
        if (!shortcut.arguments.empty() ||
            !launcher_target_in_layout(m_Paths, shortcut.target))
        {
            return false;
        }
        try
        {
            reject_reparse_chain(shortcut.target,
                "installed UVSR Launcher shortcut target");
            const OwnerMarker marker = parse_owner(read_text(
                m_Paths.program_marker(), MaximumStateBytes));
            return guid_equal(marker.installationId, installationId);
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    ShortcutInfo ShellIntegration::read_shortcut(const fs::path& path)
    {
        IShellLinkW* raw = nullptr;
        HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw));
        if (FAILED(result) || raw == nullptr)
            throw Error("Windows could not create its shortcut service.");
        Com<IShellLinkW> link(raw);
        IPersistFile* persistRaw = nullptr;
        result = link.get()->QueryInterface(IID_PPV_ARGS(&persistRaw));
        if (FAILED(result) || persistRaw == nullptr)
            throw Error("Windows could not inspect a shortcut.");
        Com<IPersistFile> persist(persistRaw);
        if (FAILED(persist.get()->Load(path.c_str(), STGM_READ)))
            throw Error("The shortcut '" + narrow(path.native()) +
                "' could not be inspected safely.");
        std::wstring target(32768u, L'\0');
        std::wstring arguments(32768u, L'\0');
        WIN32_FIND_DATAW find{};
        if (FAILED(link.get()->GetPath(target.data(),
                static_cast<int>(target.size()), &find, SLGP_RAWPATH)) ||
            FAILED(link.get()->GetArguments(arguments.data(),
                static_cast<int>(arguments.size()))))
        {
            throw Error("The shortcut '" + narrow(path.native()) +
                "' could not be inspected safely.");
        }
        target.resize(wcsnlen_s(target.c_str(), target.size()));
        arguments.resize(wcsnlen_s(arguments.c_str(), arguments.size()));
        return { target, arguments };
    }

    void ShellIntegration::create_shortcut(
        const fs::path& path,
        const fs::path& target,
        std::wstring_view arguments,
        const fs::path& workingDirectory,
        std::wstring_view description)
    {
        fs::create_directories(path.parent_path());
        const fs::path temporary = path.native() + L"." +
            compact_guid(new_guid()) + L".tmp";
        IShellLinkW* raw = nullptr;
        HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw));
        if (FAILED(result) || raw == nullptr)
            throw Error("Windows could not create its shortcut service.");
        Com<IShellLinkW> link(raw);
        if (FAILED(link.get()->SetPath(target.c_str())) ||
            FAILED(link.get()->SetArguments(std::wstring(arguments).c_str())) ||
            FAILED(link.get()->SetWorkingDirectory(workingDirectory.c_str())) ||
            FAILED(link.get()->SetDescription(std::wstring(description).c_str())) ||
            FAILED(link.get()->SetIconLocation(target.c_str(), 0)))
        {
            throw Error("Windows could not prepare a UVSR shortcut.");
        }
        IPersistFile* persistRaw = nullptr;
        result = link.get()->QueryInterface(IID_PPV_ARGS(&persistRaw));
        if (FAILED(result) || persistRaw == nullptr)
            throw Error("Windows could not save a UVSR shortcut.");
        Com<IPersistFile> persist(persistRaw);
        try
        {
            if (FAILED(persist.get()->Save(temporary.c_str(), TRUE)))
                throw Error("Windows could not save a UVSR shortcut.");
            if (!MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                throw_last_error("Windows could not activate a UVSR shortcut");
            }
        }
        catch (...)
        {
            DeleteFileW(temporary.c_str());
            throw;
        }
    }

    void ShellIntegration::remove_staged_registry(const GUID& transactionId)
    {
        Registry parent = open_registry(HKEY_CURRENT_USER,
            UninstallParent, KEY_READ | KEY_WRITE);
        if (parent)
            delete_registry_tree(parent.get(), staging_registry_name(transactionId));
    }
}
