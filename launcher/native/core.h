#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "strict_json_contract.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stop_token>

namespace uvsr::launcher
{
    namespace fs = std::filesystem;
    using Json = json::Value;
    using namespace contract;
    inline constexpr char ProductId[] = "0c47a7a8-1ec4-4ffd-b6c4-2f7614181223";
    inline constexpr char KeyId[] = "uvsr-launcher-update-p256-2026-01";
    inline constexpr char PublicKey[] = "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEATbHkDwYIS0nMut5h9Q6m67qfabhuK+VRo6mDW1UlwZQIfeLI7zc1aKblCclkfgd8DDU0LcblFgTFdvoAWgCYg==";
    inline constexpr char LauncherVersion[] = "1.3.0";
    inline constexpr int64_t LauncherSequence = 17;
    inline constexpr int64_t MaximumSequence = 9007199254740991ll;
    inline constexpr uint64_t MaximumLauncherBytes = 256ull << 20;
    inline constexpr uint64_t MaximumArchiveBytes = 32ull << 30;
    inline constexpr uint64_t MaximumExpandedBytes = 64ull << 30;
    inline constexpr char LauncherName[] = "uvsr-launcher.exe";
    inline constexpr char EngineName[] = "uvsr-engine.exe";
    inline constexpr char ArchiveName[] = "uvsr-renderer-windows-11-x64.zip";
    inline constexpr char OwnerName[] = ".uvsr-installer-owner.json";
    inline constexpr char PackageName[] = "package-manifest.json";
    inline constexpr char LauncherPackageName[] = ".uvsr-launcher-package.json";
    inline constexpr char LauncherFeedUrl[] = "https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/launcher-update-feed-v2.json";
    inline constexpr char RendererFeedUrl[] = "https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/renderer-update-feed-v1.json";

    inline void Require(bool condition, std::string_view message)
    {
        if (!condition) throw std::runtime_error(std::string(message));
    }
    void WinCheck(BOOL result, std::string_view operation);
    struct AccessError : std::runtime_error
    {
        DWORD code;
        AccessError(std::string message, DWORD error) : std::runtime_error(std::move(message)), code(error) {}
    };
    std::wstring Wide(std::string_view text);
    std::string Utf8(std::wstring_view text);
    std::string Lower(std::string value);
    std::string Guid();
    bool IsGuid(std::string_view value);
    std::string UtcNow();
    bool IsVersionId(std::string_view value);
    std::string NewVersionId(std::string_view commit);
    std::string QuoteJson(std::string_view text);
    Json JString(std::string text);
    Json JNumber(int64_t value);
    Json JBool(bool value);
    Json JObject(std::initializer_list<std::pair<std::string, Json>> values);
    std::string Serialize(const Json& value);
    const std::string& Text(const Json& value, std::string_view name);
    int64_t Number(const Json& value, std::string_view name);
    bool Flag(const Json& value, std::string_view name);
    void Set(Json& value, std::string_view name, Json replacement);

    struct Handle
    {
        HANDLE value = nullptr;
        Handle() = default;
        explicit Handle(HANDLE handle) : value(handle) {}
        ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
        operator HANDLE() const { return value; }
    };
    void RejectReparseChain(const fs::path& path);
    bool IsDescendant(const fs::path& path, const fs::path& root);
    fs::path Descendant(const fs::path& root, std::string_view relative);
    void ValidateRelativePath(std::string_view relative);
    void CreateDirectories(const fs::path& path);
    Json ReadRecord(const fs::path& path, uint64_t maximum = 65536);
    void WriteAtomic(const fs::path& path, std::string_view bytes);
    void WriteRecord(const fs::path& path, const Json& value);
    std::string HashFile(const fs::path& path);
    std::string HashHandle(HANDLE file);
    std::vector<unsigned char> HashBytes(std::span<const unsigned char> bytes);
    bool HashEqual(std::string_view left, std::string_view right);
    void VerifyFile(const fs::path& path, uint64_t size, std::string_view hash);
    void ValidatePe(const fs::path& path);
    std::string PeString(const fs::path& path, std::wstring_view field);
    fs::path CurrentExecutable();
    void EnsurePlatform();

    struct Paths
    {
        fs::path local, program, state, desktop, programs, operations;
        static Paths Create(const fs::path& local, const fs::path& desktop, const fs::path& programs);
        static Paths CurrentUser();
        fs::path Versions() const { return program / "versions"; }
        fs::path LauncherVersions() const { return program / "launcher/versions"; }
        fs::path Renderer(std::string_view version) const;
        fs::path Launcher(std::string_view hash) const;
        fs::path DesktopShortcut() const { return desktop / "UVSR Launcher.lnk"; }
        fs::path StartShortcut() const { return programs / "UVSR/UVSR Launcher.lnk"; }
    };
    std::optional<std::string> InspectOwnership(const Paths& paths);
    std::string EnsureOwnership(const Paths& paths);
    void EnsureOwnedRoot(const fs::path& root, std::string_view installation);
    class OperationLock
    {
        Handle mutex;
    public:
        explicit OperationLock(std::wstring_view suffix = {});
        ~OperationLock();
    };

    enum class Component { Renderer, Launcher };
    struct Feed
    {
        Component component = Component::Renderer;
        int64_t sequence = 0;
        std::string commit, version, settingsHash, hash;
        uint64_t size = 0;
        bool operator==(const Feed&) const = default;
    };
    Feed VerifyFeed(std::string_view envelope, Component component,
        std::string_view publicKey = PublicKey, std::string_view keyId = KeyId);
    void VerifyP256Signature(std::span<const unsigned char> payload,
        std::span<const unsigned char> signature, std::string_view publicKey);
    std::string ArtifactUrl(const Feed& feed);
    void ValidateState(const Json& state, std::string_view installation, Component component);
    enum class UpdateState { NotInstalled, Current, UpdateAvailable, RepairNeeded, CheckFailed };
    UpdateState Classify(const std::optional<Json>& recorded, bool healthy, const Feed& feed);
    void ValidateLauncherMetadata(const fs::path& path, std::string_view version,
        std::optional<std::string_view> commit = {});

    struct Progress
    {
        std::string phase, detail;
        std::optional<int> percent;
        bool canCancel = true;
    };
    using Report = std::function<void(const Progress&)>;
    inline void CheckCancelled(std::stop_token stop)
    {
        Require(!stop.stop_requested(), "The operation was cancelled. The previous installed UVSR version was preserved.");
    }
    void Download(std::string_view url, const fs::path& destination,
        uint64_t maximum, std::optional<std::string_view> hash,
        std::stop_token stop, const Report& report);
    struct PackageFile { std::string path, hash; uint64_t size = 0; };
    struct Package { Json manifest; std::vector<PackageFile> files; };
    bool AllowedPackagePath(std::string_view path, bool directory = false);
    Package ValidatePackage(const fs::path& root, const Feed* feed = nullptr);
    void ExtractPackage(const fs::path& archive, const fs::path& root,
        std::stop_token stop, const Report& report, const Feed* feed = nullptr);
}
