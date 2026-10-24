#include "installer.h"
#include <shlobj.h>
#include <wrl/client.h>
#include <map>

namespace uvsr::launcher
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        struct Com
        {
            HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            Com() { Require(SUCCEEDED(result) || result == RPC_E_CHANGED_MODE, "Windows shell services are unavailable."); }
            ~Com() { if (SUCCEEDED(result)) CoUninitialize(); }
        };
        struct Key
        {
            HKEY value = nullptr;
            ~Key() { if (value) RegCloseKey(value); }
            operator HKEY() const { return value; }
        };
        void RegistryCheck(LSTATUS status)
        {
            if (status != ERROR_SUCCESS) throw AccessError("Windows could not update the per-user Apps & Features record (error " + std::to_string(status) + ").", DWORD(status));
        }
        bool Open(Key& key, const std::wstring& path, REGSAM access = KEY_READ)
        {
            auto status = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, access, &key.value);
            if (status == ERROR_FILE_NOT_FOUND) return false;
            RegistryCheck(status); return true;
        }
        std::wstring Value(HKEY key, const wchar_t* name)
        {
            DWORD size = 0;
            auto status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size);
            if (status == ERROR_FILE_NOT_FOUND) return {};
            RegistryCheck(status); Require(size <= 65536 && size % 2 == 0, "A registry ownership field is invalid.");
            std::wstring value(size / 2, 0);
            RegistryCheck(RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value.data(), &size));
            while (!value.empty() && value.back() == 0) value.pop_back(); return value;
        }
        bool Owned(HKEY key, std::string_view installation)
        { return Lower(Utf8(Value(key, L"UVSRProductId"))) == ProductId && Lower(Utf8(Value(key, L"UVSRInstallId"))) == installation; }
        bool TransactionOwned(HKEY key, std::string_view transaction)
        { return !transaction.empty() && Lower(Utf8(Value(key, L"UVSRTransactionId"))) == transaction; }
        void Write(HKEY key, const wchar_t* name, std::wstring_view value)
        {
            std::wstring terminated(value);
            RegistryCheck(RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(terminated.c_str()), DWORD((terminated.size() + 1) * sizeof(wchar_t))));
        }
        void Write(HKEY key, const wchar_t* name, DWORD value)
        { RegistryCheck(RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value))); }
        struct RegistryValue { DWORD type = 0; std::vector<unsigned char> bytes; };
        using RegistrySnapshot = std::optional<std::map<std::wstring, RegistryValue>>;
        RegistrySnapshot CaptureRegistry(const std::wstring& path)
        {
            Key key; if (!Open(key, path)) return {};
            RegistrySnapshot snapshot = std::map<std::wstring, RegistryValue>{};
            for (DWORD index = 0;; ++index)
            {
                std::wstring name(16384, 0); DWORD nameSize = DWORD(name.size()), size = 0, type = 0;
                auto status = RegEnumValueW(key, index, name.data(), &nameSize, nullptr, &type, nullptr, &size);
                if (status == ERROR_NO_MORE_ITEMS) break;
                RegistryCheck(status); Require(index < 256 && size <= (1u << 20), "The Apps & Features snapshot exceeds its safe limit.");
                name.resize(nameSize); RegistryValue value{type, std::vector<unsigned char>(size)};
                RegistryCheck(RegQueryValueExW(key, name.c_str(), nullptr, &value.type, value.bytes.data(), &size));
                value.bytes.resize(size); snapshot->emplace(std::move(name), std::move(value));
            }
            return snapshot;
        }
        void RestoreRegistry(const std::wstring& path, const RegistrySnapshot& snapshot, std::string_view installation, std::string_view transaction)
        {
            Key key;
            if (Open(key, path, KEY_READ | KEY_WRITE))
                Require(Owned(key, installation) || TransactionOwned(key, transaction), "The registry record changed ownership during rollback. It was preserved.");
            if (!snapshot)
            {
                if (key.value) RegistryCheck(RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()));
                return;
            }
            if (!key.value) RegistryCheck(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key.value, nullptr));
            auto current = CaptureRegistry(path);
            if (current) for (const auto& [name, value] : *current)
                if (!snapshot->contains(name)) RegistryCheck(RegDeleteValueW(key, name.c_str()));
            for (const auto& [name, value] : *snapshot)
                RegistryCheck(RegSetValueExW(key, name.c_str(), 0, value.type, value.bytes.data(), DWORD(value.bytes.size())));
            RegistryCheck(RegFlushKey(key));
        }
        struct Shortcut { fs::path target; std::wstring arguments; };
        Shortcut ReadShortcut(const fs::path& path)
        {
            Com com; ComPtr<IShellLinkW> link;
            Require(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))), "Windows could not read a shortcut.");
            ComPtr<IPersistFile> file; Require(SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Load(path.c_str(), STGM_READ)), "A shortcut cannot be inspected safely.");
            std::wstring target(32768, 0), arguments(32768, 0);
            // use the filesystem target, not raw environment-variable text. never resolve or launch the link.
            Require(SUCCEEDED(link->GetPath(target.data(), int(target.size()), nullptr, 0)) &&
                SUCCEEDED(link->GetArguments(arguments.data(), int(arguments.size()))), "A shortcut cannot be inspected safely.");
            target.resize(wcslen(target.c_str())); arguments.resize(wcslen(arguments.c_str())); return {target, arguments};
        }
        void CheckShellPath(const Paths& paths, const fs::path& path)
        {
            const auto& root = IsDescendant(path, paths.desktop) ? paths.desktop : paths.programs;
            Require(IsDescendant(path, root), "A shortcut escaped its Windows known-folder boundary.");
            fs::path current = root;
            for (const auto& part : path.lexically_relative(root))
            {
                current /= part; const auto attributes = GetFileAttributesW(current.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES)
                { auto error = GetLastError(); Require(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND, "A shortcut path cannot be inspected."); }
                else Require(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "A shortcut path crosses an unexpected link. It was preserved.");
            }
        }
        void SaveShortcut(const Paths& paths, const fs::path& destination, const fs::path& executable, std::wstring_view description,
            std::optional<std::string_view> expectedHash = {})
        {
            CheckShellPath(paths, destination);
            fs::create_directories(destination.parent_path()); CheckShellPath(paths, destination);
            const auto temporary = destination.parent_path() / (L".uvsr-" + Wide(Guid()) + L".lnk");
            Com com; ComPtr<IShellLinkW> link;
            Require(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))), "Windows could not create a shortcut.");
            auto descriptionText = std::wstring(description);
            Require(SUCCEEDED(link->SetPath(executable.c_str())) && SUCCEEDED(link->SetArguments(L"")) &&
                SUCCEEDED(link->SetWorkingDirectory(executable.parent_path().c_str())) && SUCCEEDED(link->SetIconLocation(executable.c_str(), 0)) &&
                SUCCEEDED(link->SetDescription(descriptionText.c_str())), "Windows could not configure a shortcut.");
            ComPtr<IPersistFile> file; Require(SUCCEEDED(link.As(&file)), "Windows could not persist a shortcut.");
            try
            {
                Require(SUCCEEDED(file->Save(temporary.c_str(), TRUE)), "Windows could not write a shortcut.");
                CheckShellPath(paths, destination);
                if (expectedHash) Require(HashEqual(HashFile(destination), *expectedHash), "The shortcut changed during migration. It was preserved.");
                WinCheck(MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_WRITE_THROUGH | (expectedHash ? MOVEFILE_REPLACE_EXISTING : 0)), "Activate shortcut");
            }
            catch (...) { DeleteFileW(temporary.c_str()); throw; }
        }
        std::optional<std::string> CaptureShortcut(const Paths& paths, const fs::path& path)
        {
            CheckShellPath(paths, path);
            return fs::exists(path) ? std::optional<std::string>(ReadFile(path, 1u << 20)) : std::nullopt;
        }
        std::wstring StageName(std::string_view transaction)
        {
            Require(IsGuid(transaction), "The shell transaction identity is invalid.");
            std::string id(transaction); std::erase(id, '-'); return L"UVSR Installer " + Wide(id) + L".staging";
        }
    }
    Shell::Shell(Paths p, std::wstring registryPath) : paths(std::move(p)), keyPath(std::move(registryPath)) {}
    bool Shell::OwnsShortcut(const fs::path& path, std::string_view installation) const
    {
        try
        {
            CheckShellPath(paths, path);
            auto shortcut = ReadShortcut(path);
            if (!shortcut.arguments.empty() || shortcut.target.empty() || Lower(Utf8(shortcut.target.filename().wstring())) != LauncherName) return false;
            const auto hash = Utf8(shortcut.target.parent_path().filename().wstring());
            if (!IsLowerHex(hash, 64) || !SamePath(shortcut.target, paths.Launcher(hash) / LauncherName)) return false;
            auto owner = InspectOwnership(paths); return owner && *owner == installation;
        }
        catch (...) { return false; }
    }
    void Shell::MigrateLegacyShortcuts(std::string_view installation, const Json& launcher) const
    {
        // the caller has validated this canonical package, its marker, PE metadata and exact file hash.
        ValidateState(launcher, installation, Component::Launcher);
        const auto owner = InspectOwnership(paths);
        Require(owner && *owner == installation, "The shortcut migration could not verify installation ownership.");
        const auto root = paths.Launcher(Text(launcher, "executableSha256"));
        const auto old = root / "UVSR Launcher.exe", executable = root / LauncherName;
        for (const auto& path : {paths.StartShortcut(), paths.DesktopShortcut()})
        {
            CheckShellPath(paths, path);
            if (!fs::exists(path)) continue;
            const auto hash = HashFile(path);
            Shortcut shortcut;
            try { shortcut = ReadShortcut(path); }
            catch (const std::exception&) { continue; }
            if (!shortcut.arguments.empty() || !shortcut.target.is_absolute() || !SamePath(shortcut.target, old)) continue;
            try
            {
                SaveShortcut(paths, path, executable,
                    path == paths.StartShortcut() ? L"Install, launch, update, or remove UVSR" : L"Open UVSR Launcher", hash);
            }
            catch (const std::exception& error)
            { throw std::runtime_error("UVSR could not migrate its old shortcut at \"" + Utf8(path.wstring()) + "\": " + error.what()); }
        }
    }
    void Shell::Validate(std::string_view installation, bool desktop) const
    {
        Key key;
        if (Open(key, keyPath)) Require(Owned(key, installation), "An unrelated Apps & Features entry already uses the UVSR name. It was preserved.");
        for (const auto& path : {paths.StartShortcut(), paths.DesktopShortcut()})
        {
            CheckShellPath(paths, path);
            if (path == paths.DesktopShortcut() && !desktop) continue;
            Require(!fs::exists(path) || fs::is_regular_file(path), "The shortcut location is occupied by a folder. It was preserved: " + Utf8(path.wstring()));
        }
    }
    void Shell::Apply(std::string_view installation, const std::optional<Json>& renderer, const Json& launcher,
        std::string_view transaction, const Report& report) const
    {
        ValidateState(launcher, installation, Component::Launcher);
        if (renderer) ValidateState(*renderer, installation, Component::Renderer);
        const bool desktop = Flag(launcher, "desktopShortcut");
        {
            Key key;
            if (Open(key, keyPath) && !Owned(key, installation))
                Require(TransactionOwned(key, transaction), "The Apps & Features record is owned by another installation.");
        }
        for (const auto& path : {paths.StartShortcut(), paths.DesktopShortcut()})
        {
            CheckShellPath(paths, path);
            if (path == paths.DesktopShortcut() && !desktop) continue;
            if (!fs::exists(path) || OwnsShortcut(path, installation)) continue;
            Require(fs::is_regular_file(path), "The shortcut location is occupied by a folder. It was preserved: " + Utf8(path.wstring()));
            const auto backup = path.parent_path() / (L"UVSR Launcher (preserved " + Wide(Guid()) + L").lnk");
            CheckShellPath(paths, backup);
            // keep the original file intact, including unreadable shortcut formats. never overwrite a backup.
            // the backup survives failures, recovery and uninstall; only canonical owned links are removed.
            WinCheck(MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH), "Preserve conflicting shortcut");
            if (report) report({"Shortcut preserved", "Your existing shortcut was kept at \"" + Utf8(backup.wstring()) +
                "\". UVSR installation can continue.", {}, false});
        }
        const auto oldStart = CaptureShortcut(paths, paths.StartShortcut()), oldDesktop = CaptureShortcut(paths, paths.DesktopShortcut());
        const auto oldRegistry = CaptureRegistry(keyPath);
        bool startChanged = false, desktopChanged = false, registryChanged = false;
        const auto executable = paths.Launcher(Text(launcher, "executableSha256")) / LauncherName;
        Require(fs::is_regular_file(executable), "The installed UVSR Launcher is incomplete.");
        const auto parentPath = keyPath.substr(0, keyPath.rfind(L'\\'));
        const auto staging = parentPath + L"\\" + StageName(transaction);
        try
        {
            const auto save = [&](const fs::path& path, std::wstring_view description)
            {
                std::optional<std::string> hash;
                if (fs::exists(path))
                {
                    Require(OwnsShortcut(path, installation), "A new unrelated shortcut appeared during installation. It was preserved. Retry installation.");
                    hash = HashFile(path);
                }
                SaveShortcut(paths, path, executable, description, hash ? std::optional<std::string_view>(*hash) : std::nullopt);
            };
            save(paths.StartShortcut(), L"Install, launch, update, or remove UVSR"); startChanged = true;
            if (desktop) { save(paths.DesktopShortcut(), L"Open UVSR Launcher"); desktopChanged = true; }
            else if (fs::exists(paths.DesktopShortcut()) && OwnsShortcut(paths.DesktopShortcut(), installation))
            { fs::remove(paths.DesktopShortcut()); desktopChanged = true; }
            const bool fresh = !oldRegistry;
            const auto& target = fresh ? staging : keyPath;
            Key key;
            if (Open(key, target, KEY_READ | KEY_WRITE))
                Require(Owned(key, installation) || TransactionOwned(key, transaction), "The registry staging name is already in use. It was preserved.");
            else RegistryCheck(RegCreateKeyExW(HKEY_CURRENT_USER, target.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key.value, nullptr));
            registryChanged = !fresh;
            Write(key, L"UVSRTransactionId", Wide(transaction));
            Write(key, L"UVSRProductId", Wide(ProductId)); Write(key, L"UVSRInstallId", Wide(installation));
            Write(key, L"DisplayName", L"UVSR Launcher"); Write(key, L"DisplayVersion", Wide(Text(launcher, "version")));
            Write(key, L"Publisher", L"UVSR"); Write(key, L"InstallLocation", paths.program.wstring());
            Write(key, L"DisplayIcon", executable.wstring());
            Write(key, L"UninstallString", L"\"" + executable.wstring() + L"\" --uninstall");
            Write(key, L"ModifyPath", L"\"" + executable.wstring() + L"\""); Write(key, L"NoRepair", DWORD(1));
            uint64_t size = fs::file_size(executable);
            if (renderer)
                for (const auto& file : fs::recursive_directory_iterator(paths.Renderer(Text(*renderer, "activeVersionId"))))
                { RejectReparseChain(file.path()); if (file.is_regular_file()) size += file.file_size(); }
            Write(key, L"EstimatedSize", DWORD(std::min<uint64_t>(INT32_MAX, size / 1024)));
            RegistryCheck(RegFlushKey(key));
            if (fresh)
            {
                Key parent; Require(Open(parent, parentPath, KEY_READ | KEY_WRITE), "The uninstall registry parent is unavailable.");
                Key collision; Require(!Open(collision, keyPath), "An unrelated Apps & Features entry appeared. It was preserved.");
                RegistryCheck(RegRenameKey(parent, StageName(transaction).c_str(), keyPath.substr(keyPath.rfind(L'\\') + 1).c_str())); registryChanged = true;
            }
            RegistryCheck(RegDeleteValueW(key, L"UVSRTransactionId")); RegistryCheck(RegFlushKey(key));
        }
        catch (...)
        {
            auto failure = std::current_exception();
            const auto restore = [&](const fs::path& path, const std::optional<std::string>& bytes, bool changed)
            {
                if (!changed) return;
                CheckShellPath(paths, path);
                Require(!fs::exists(path) || OwnsShortcut(path, installation), "A shortcut changed ownership during rollback. It was preserved.");
                if (bytes)
                {
                    const auto temporary = path.parent_path() / (L".uvsr-" + Wide(Guid()) + L".lnk");
                    Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_WRITE_THROUGH, nullptr));
                    WinCheck(file.value != INVALID_HANDLE_VALUE, "Restore shortcut"); DWORD written = 0;
                    WinCheck(WriteFile(file, bytes->data(), DWORD(bytes->size()), &written, nullptr) && written == bytes->size(), "Restore shortcut");
                    WinCheck(FlushFileBuffers(file), "Flush restored shortcut");
                    CloseHandle(file.value); file.value = nullptr;
                    WinCheck(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Restore shortcut activation");
                }
                else fs::remove(path);
            };
            try
            {
                if (registryChanged) RestoreRegistry(keyPath, oldRegistry, installation, transaction);
                restore(paths.StartShortcut(), oldStart, startChanged); restore(paths.DesktopShortcut(), oldDesktop, desktopChanged);
                RemoveStaged(transaction);
            }
            catch (...) { throw std::runtime_error("Windows shell integration failed and could not be restored completely. UVSR Launcher will retry recovery next time it opens."); }
            std::rethrow_exception(failure);
        }
    }
    void Shell::Remove(std::string_view installation) const
    {
        Key key;
        if (Open(key, keyPath, KEY_READ | KEY_WRITE))
        { Require(Owned(key, installation), "The Apps & Features entry is unrelated. It was preserved."); RegistryCheck(RegDeleteTreeW(HKEY_CURRENT_USER, keyPath.c_str())); }
        for (const auto& path : {paths.DesktopShortcut(), paths.StartShortcut()})
        { CheckShellPath(paths, path); if (fs::exists(path) && OwnsShortcut(path, installation)) fs::remove(path); }
        CheckShellPath(paths, paths.StartShortcut());
        if (fs::is_directory(paths.StartShortcut().parent_path()) && fs::is_empty(paths.StartShortcut().parent_path())) fs::remove(paths.StartShortcut().parent_path());
    }
    void Shell::RemoveStaged(std::string_view transaction) const
    {
        const auto staging = keyPath.substr(0, keyPath.rfind(L'\\')) + L"\\" + StageName(transaction);
        Key key;
        if (Open(key, staging, KEY_READ | KEY_WRITE))
        {
            Require(TransactionOwned(key, transaction), "The staged registry record could not prove its transaction. It was preserved.");
            RegistryCheck(RegDeleteTreeW(HKEY_CURRENT_USER, staging.c_str()));
        }
    }
}
