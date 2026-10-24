#include "core.h"
#include <bcrypt.h>
#include <sddl.h>
#include <shlobj.h>
#include <winternl.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace uvsr::launcher
{
    void WinCheck(BOOL result, std::string_view operation)
    {
        if (!result)
        {
            const DWORD code = GetLastError();
            throw AccessError(std::string(operation) + " failed (Windows error " + std::to_string(code) + ").", code);
        }
    }
    std::wstring Wide(std::string_view text)
    {
        if (text.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
        WinCheck(size != 0, "Decode UTF-8");
        std::wstring result(size, 0);
        WinCheck(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), size), "Decode UTF-8");
        return result;
    }
    std::string Utf8(std::wstring_view text)
    {
        if (text.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
        WinCheck(size != 0, "Encode UTF-8");
        std::string result(size, 0);
        WinCheck(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), size, nullptr, nullptr), "Encode UTF-8");
        return result;
    }
    std::string Lower(std::string value)
    {
        for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return value;
    }
    std::string Guid()
    {
        GUID value{};
        Require(SUCCEEDED(CoCreateGuid(&value)), "Windows could not create an installation identity.");
        wchar_t buffer[39]{};
        StringFromGUID2(value, buffer, 39);
        return Lower(Utf8(std::wstring_view(buffer + 1, 36)));
    }
    bool IsGuid(std::string_view text)
    {
        if (text.size() != 36 || text == "00000000-0000-0000-0000-000000000000") return false;
        for (size_t i = 0; i < text.size(); ++i)
            if (i == 8 || i == 13 || i == 18 || i == 23)
            { if (text[i] != '-') return false; }
            else if (!IsLowerHex(text.substr(i, 1), 1)) return false;
        return true;
    }
    std::string UtcNow()
    {
        SYSTEMTIME t{}; GetSystemTime(&t);
        char buffer[40]{};
        snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03u+00:00",
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        return buffer;
    }
    bool IsVersionId(std::string_view value)
    {
        return value.size() == 64 && IsLowerHex(value.substr(0, 40), 40) && value[40] == '-' &&
            value[55] == '-' && IsLowerHex(value.substr(56), 8) &&
            value.substr(41, 14).find_first_not_of("0123456789") == std::string_view::npos;
    }
    std::string NewVersionId(std::string_view commit)
    {
        Require(IsLowerHex(commit, 40), "The source commit is invalid.");
        auto time = UtcNow();
        return std::string(commit) + "-" + time.substr(0,4) + time.substr(5,2) + time.substr(8,2) +
            time.substr(11,2) + time.substr(14,2) + time.substr(17,2) + "-" + Guid().substr(0,8);
    }
    std::string QuoteJson(std::string_view text) { return "\"" + json::Escape(text) + "\""; }
    Json JString(std::string text) { Json v; v.kind = Json::Kind::String; v.string = std::move(text); return v; }
    Json JNumber(int64_t value) { Json v; v.kind = Json::Kind::Number; v.string = std::to_string(value); return v; }
    Json JBool(bool value) { Json v; v.kind = Json::Kind::Boolean; v.boolean = value; return v; }
    Json JObject(std::initializer_list<std::pair<std::string, Json>> values)
    { Json v; v.kind = Json::Kind::Object; v.object = values; return v; }
    std::string Serialize(const Json& value)
    {
        if (value.kind == Json::Kind::String) return QuoteJson(value.string);
        if (value.kind == Json::Kind::Number) return value.string;
        if (value.kind == Json::Kind::Boolean) return value.boolean ? "true" : "false";
        if (value.kind == Json::Kind::Null) return "null";
        std::string result = value.kind == Json::Kind::Object ? "{" : "[";
        bool first = true;
        const auto append = [&](std::string text) { if (!first) result += ','; first = false; result += text; };
        for (const auto& [name, member] : value.object) append(QuoteJson(name) + ':' + Serialize(member));
        for (const auto& item : value.array) append(Serialize(item));
        return result + (value.kind == Json::Kind::Object ? "}" : "]");
    }
    const std::string& Text(const Json& value, std::string_view name) { return String(Member(value, name), name); }
    int64_t Number(const Json& value, std::string_view name) { return Integer(Member(value, name), name); }
    bool Flag(const Json& value, std::string_view name) { return Boolean(Member(value, name), name); }
    void Set(Json& value, std::string_view name, Json replacement)
    {
        for (auto& member : value.object)
            if (member.first == name) { member.second = std::move(replacement); return; }
        throw std::runtime_error("Missing state field " + std::string(name));
    }
    void RejectReparseChain(const fs::path& input)
    {
        const auto path = fs::absolute(input).lexically_normal();
        fs::path current;
        for (const auto& part : path)
        {
            current /= part;
            if (current == path.root_name()) continue;
            DWORD attributes = GetFileAttributesW(current.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                DWORD error = GetLastError();
                Require(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND,
                    "A UVSR path could not be inspected safely.");
            }
            else Require(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "A UVSR path crosses a link or reparse point. It was preserved.");
        }
    }
    bool IsDescendant(const fs::path& path, const fs::path& root)
    {
        auto p = fs::absolute(path).lexically_normal().wstring();
        auto r = fs::absolute(root).lexically_normal().wstring();
        while (!r.empty() && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
        r += L'\\';
        return p.size() > r.size() && CompareStringOrdinal(p.data(), int(r.size()), r.data(), int(r.size()), TRUE) == CSTR_EQUAL;
    }
    void ValidateRelativePath(std::string_view value)
    {
        Require(!value.empty() && value.size() < 32760 && value.find_first_of("\\:*?\"<>|") == value.npos,
            "A package path is unsafe.");
        size_t begin = 0;
        while (begin < value.size())
        {
            size_t end = value.find('/', begin); if (end == value.npos) end = value.size();
            auto part = value.substr(begin, end - begin);
            Require(!part.empty() && part != "." && part != ".." && part.back() != '.' && part.back() != ' ', "A package path is not canonical.");
            for (unsigned char c : part) Require(c >= 32 && c != 127, "A package path contains control characters.");
            auto base = Lower(std::string(part.substr(0, part.find('.'))));
            Require(base != "con" && base != "prn" && base != "aux" && base != "nul" &&
                !(base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) && base[3] >= '0' && base[3] <= '9'),
                "A package path names a Windows device.");
            begin = end + 1;
        }
        Require(value.back() != '/', "A file path ends with a separator.");
        (void)Wide(value);
    }
    fs::path Descendant(const fs::path& root, std::string_view relative)
    {
        ValidateRelativePath(relative);
        fs::path result = root / Wide(relative);
        Require(IsDescendant(result, root), "A package path escaped its owned root.");
        RejectReparseChain(result);
        return result;
    }
    void CreateDirectories(const fs::path& path)
    { RejectReparseChain(path); fs::create_directories(path); RejectReparseChain(path); }
    Json ReadRecord(const fs::path& path, uint64_t maximum)
    {
        RejectReparseChain(path);
        Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        WinCheck(file.value != INVALID_HANDLE_VALUE, "Open state record");
        LARGE_INTEGER size{}; WinCheck(GetFileSizeEx(file, &size), "Measure state record");
        Require(size.QuadPart >= 0 && uint64_t(size.QuadPart) <= maximum && uint64_t(size.QuadPart) <= UINT32_MAX, "The state record exceeds its limit.");
        std::string bytes(size_t(size.QuadPart), '\0'); DWORD count = 0;
        WinCheck(::ReadFile(file, bytes.data(), DWORD(bytes.size()), &count, nullptr) && count == bytes.size(), "Read state record");
        return json::Parser(bytes, 32).Parse();
    }
    void WriteAtomic(const fs::path& path, std::string_view bytes)
    {
        CreateDirectories(path.parent_path()); RejectReparseChain(path);
        fs::path temporary = path.parent_path() / (L"." + path.filename().wstring() + L"." + Wide(Guid()) + L".tmp");
        try
        {
            {
                Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
                WinCheck(file.value != INVALID_HANDLE_VALUE, "Create durable record");
                Require(bytes.size() <= UINT32_MAX, "The record exceeds its limit.");
                DWORD written = 0;
                WinCheck(WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size(), "Write durable record");
                WinCheck(FlushFileBuffers(file), "Flush durable record");
            }
            WinCheck(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Activate durable record");
        }
        catch (...) { DeleteFileW(temporary.c_str()); throw; }
    }
    void WriteRecord(const fs::path& path, const Json& value) { WriteAtomic(path, Serialize(value) + "\n"); }
    namespace
    {
        struct Hash
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            Hash()
            {
                Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "SHA-256 is unavailable.");
                if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
                { BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr; throw std::runtime_error("SHA-256 initialization failed."); }
            }
            ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
            void Add(std::span<const unsigned char> bytes)
            { Require(bytes.size() <= ULONG_MAX && BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), ULONG(bytes.size()), 0) >= 0, "SHA-256 failed."); }
            std::vector<unsigned char> Finish()
            { std::vector<unsigned char> result(32); Require(BCryptFinishHash(hash, result.data(), 32, 0) >= 0, "SHA-256 failed."); return result; }
        };
    }
    std::vector<unsigned char> HashBytes(std::span<const unsigned char> bytes)
    { Hash hash; hash.Add(bytes); return hash.Finish(); }
    std::string HashFile(const fs::path& path)
    {
        RejectReparseChain(path);
        Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        WinCheck(file.value != INVALID_HANDLE_VALUE, "Open file for integrity check");
        return HashHandle(file);
    }
    std::string HashHandle(HANDLE file)
    {
        LARGE_INTEGER beginning{}; WinCheck(SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN), "Seek integrity input");
        Hash hash; std::array<unsigned char, 128 * 1024> buffer{};
        DWORD count = 0;
        do { WinCheck(::ReadFile(file, buffer.data(), DWORD(buffer.size()), &count, nullptr), "Read file for integrity check"); hash.Add({buffer.data(), count}); } while (count);
        std::string result;
        for (unsigned char value : hash.Finish()) { result += "0123456789abcdef"[value >> 4]; result += "0123456789abcdef"[value & 15]; }
        return result;
    }
    bool HashEqual(std::string_view left, std::string_view right)
    {
        if (!IsLowerHex(left, 64) || !IsLowerHex(right, 64)) return false;
        unsigned difference = 0;
        for (size_t i = 0; i < 64; ++i) difference |= unsigned(left[i] ^ right[i]);
        return difference == 0;
    }
    void VerifyFile(const fs::path& path, uint64_t size, std::string_view hash)
    { RejectReparseChain(path); Require(fs::is_regular_file(path) && fs::file_size(path) == size && HashEqual(HashFile(path), hash), "A downloaded or installed file failed its size or SHA-256 check."); }
    void ValidatePe(const fs::path& path)
    {
        RejectReparseChain(path);
        std::ifstream stream(path, std::ios::binary);
        IMAGE_DOS_HEADER dos{}; stream.read(reinterpret_cast<char*>(&dos), sizeof(dos));
        Require(stream.good() && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= sizeof(dos) && uint64_t(dos.e_lfanew) + 26 < fs::file_size(path), "The file is not a Windows program.");
        stream.seekg(dos.e_lfanew);
        DWORD signature = 0; IMAGE_FILE_HEADER header{}; WORD magic = 0;
        stream.read(reinterpret_cast<char*>(&signature), sizeof(signature));
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));
        stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        Require(stream.good() && signature == IMAGE_NT_SIGNATURE && header.Machine == IMAGE_FILE_MACHINE_AMD64 && magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC,
            "The file is not an x64 Windows program.");
    }
    std::string PeString(const fs::path& path, std::wstring_view field)
    {
        DWORD ignored = 0, size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        Require(size > 0 && size < (1u << 20), "The executable has no valid version resource.");
        std::vector<unsigned char> data(size);
        WinCheck(GetFileVersionInfoW(path.c_str(), 0, size, data.data()), "Read executable metadata");
        struct Translation { WORD language, codepage; };
        Translation* translations = nullptr; UINT length = 0;
        WinCheck(VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length), "Read metadata language");
        Require(length >= sizeof(Translation), "The executable metadata language is missing.");
        std::string result;
        for (size_t i = 0; i < length / sizeof(Translation); ++i)
        {
            wchar_t prefix[64]{};
            swprintf_s(prefix, L"\\StringFileInfo\\%04x%04x\\", translations[i].language, translations[i].codepage);
            auto query = std::wstring(prefix) + std::wstring(field);
            wchar_t* value = nullptr; UINT count = 0;
            WinCheck(VerQueryValueW(data.data(), query.c_str(), reinterpret_cast<void**>(&value), &count), "Read executable identity");
            Require(count > 0 && value[count - 1] == 0, "The executable identity is malformed.");
            auto current = Utf8({value, count - 1});
            Require(i == 0 || current == result, "The executable has conflicting translated identities.");
            result = std::move(current);
        }
        return result;
    }
    fs::path CurrentExecutable()
    {
        std::wstring path(32768, 0); DWORD count = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        WinCheck(count && count < path.size(), "Locate launcher executable"); path.resize(count); return path;
    }
    void EnsurePlatform()
    {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
        auto getVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        OSVERSIONINFOEXW version{}; version.dwOSVersionInfoSize = sizeof(version);
        Require(getVersion && getVersion(&version) == 0 && version.dwBuildNumber >= 22000 && version.wProductType == VER_NT_WORKSTATION,
            "UVSR Launcher currently supports only Windows 11 client editions on x64 computers.");
        Handle token; WinCheck(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value), "Read current user");
        TOKEN_ELEVATION elevation{}; DWORD returned = 0;
        WinCheck(GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned), "Check elevation");
        Require(!elevation.TokenIsElevated, "UVSR Launcher must run as your normal Windows user. Close this copy, then open it normally (do not choose Run as administrator).");
    }
    Paths Paths::Create(const fs::path& local, const fs::path& desktop, const fs::path& programs)
    {
        Paths p{fs::absolute(local).lexically_normal(), {}, {}, fs::absolute(desktop).lexically_normal(), fs::absolute(programs).lexically_normal(), {}};
        p.program = p.local / "Programs/UVSR"; p.state = p.local / "UVSR Installer"; p.operations = p.local / "UVSR Installer Operations";
        for (const auto& path : {p.program, p.state, p.operations})
        { Require(IsDescendant(path, p.local), "Windows returned an unsafe application-data location."); RejectReparseChain(path); }
        return p;
    }
    Paths Paths::CurrentUser()
    {
        const auto folder = [](const KNOWNFOLDERID& id)
        {
            PWSTR text = nullptr; Require(SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &text)), "Windows could not locate a user folder.");
            fs::path result(text); CoTaskMemFree(text); return result;
        };
        return Create(folder(FOLDERID_LocalAppData), folder(FOLDERID_Desktop), folder(FOLDERID_Programs));
    }
    fs::path Paths::Renderer(std::string_view version) const
    { Require(IsVersionId(version), "The renderer version identifier is invalid."); return Descendant(Versions(), version); }
    fs::path Paths::Launcher(std::string_view hash) const
    { Require(IsLowerHex(hash, 64), "The launcher package identifier is invalid."); return Descendant(LauncherVersions(), hash); }
    namespace
    {
        std::optional<std::string> RootOwner(const fs::path& root)
        {
            RejectReparseChain(root);
            if (!fs::exists(root)) return {};
            Require(fs::is_directory(root), "An installation root is not a directory.");
            if (!fs::exists(root / OwnerName))
            { Require(fs::is_empty(root), "An existing directory is not owned by UVSR Launcher. It was preserved."); return {}; }
            auto marker = ReadRecord(root / OwnerName);
            RequireExactObject(marker, {"schemaVersion", "productId", "installationId"}, "owner marker");
            const auto id = Text(marker, "installationId");
            Require(Number(marker, "schemaVersion") == 1 && Lower(Text(marker, "productId")) == ProductId && IsGuid(id), "The installation ownership record is invalid.");
            return id;
        }
    }
    std::optional<std::string> InspectOwnership(const Paths& p)
    {
        auto program = RootOwner(p.program), state = RootOwner(p.state);
        Require(!program || !state || program == state, "UVSR ownership records do not match. No files were changed.");
        return program ? program : state;
    }
    void EnsureOwnedRoot(const fs::path& root, std::string_view installation)
    {
        auto existing = RootOwner(root);
        Require(!existing || *existing == installation, "UVSR ownership records do not match.");
        if (!existing) WriteRecord(root / OwnerName, JObject({{"schemaVersion", JNumber(1)}, {"productId", JString(ProductId)}, {"installationId", JString(std::string(installation))}}));
    }
    std::string EnsureOwnership(const Paths& p)
    {
        auto id = InspectOwnership(p).value_or(Guid());
        EnsureOwnedRoot(p.program, id); EnsureOwnedRoot(p.state, id); CreateDirectories(p.state / "logs"); return id;
    }
    OperationLock::OperationLock(std::wstring_view suffix)
    {
        Handle token; WinCheck(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value), "Identify operation owner");
        DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<unsigned char> buffer(size);
        WinCheck(GetTokenInformation(token, TokenUser, buffer.data(), size, &size), "Identify operation owner");
        LPWSTR sid = nullptr;
        WinCheck(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid), "Identify operation owner");
        auto name = L"Global\\UVSR.Installer." + Wide(ProductId) + L"." + sid + std::wstring(suffix);
        LocalFree(sid);
        mutex.value = CreateMutexW(nullptr, FALSE, name.c_str()); WinCheck(mutex.value != nullptr, "Open operation lock");
        auto result = WaitForSingleObject(mutex, 0);
        Require(result == WAIT_OBJECT_0 || result == WAIT_ABANDONED, "Another UVSR install, update, repair, or uninstall is already running.");
    }
    OperationLock::~OperationLock() { if (mutex.value) ReleaseMutex(mutex); }
}
