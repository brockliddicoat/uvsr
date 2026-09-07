#include "fixtures.h"
#include "runtime_inventory.h"
#include "settings_snapshot.h"
#include <wincrypt.h>
#include <zlib.h>
#include <array>
#include <fstream>

namespace test
{
    namespace
    {
        std::string Base64(std::span<const unsigned char> bytes)
        {
            DWORD size = 0; WinCheck(CryptBinaryToStringA(bytes.data(), DWORD(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size), "Encode test signature");
            std::string result(size, '\0'); WinCheck(CryptBinaryToStringA(bytes.data(), DWORD(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &size), "Encode test signature");
            result.resize(size); return result;
        }
        std::span<const unsigned char> Bytes(std::string_view value) { return {reinterpret_cast<const unsigned char*>(value.data()), value.size()}; }
        void Little(std::string& bytes, uint64_t value, size_t count)
        { for (size_t i = 0; i < count; ++i) bytes += char(value >> (8 * i)); }
    }
    SigningKey::SigningKey()
    {
        Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) >= 0 &&
            BCryptGenerateKeyPair(algorithm, &value, 256, 0) >= 0 && BCryptFinalizeKeyPair(value, 0) >= 0, "create local test key");
        std::array<unsigned char, sizeof(BCRYPT_ECCKEY_BLOB) + 64> blob{}; ULONG size = 0;
        Require(BCryptExportKey(value, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(), ULONG(blob.size()), &size, 0) >= 0, "export local test key");
        std::vector<unsigned char> der{0x30,0x59,0x30,0x13,0x06,0x07,0x2a,0x86,0x48,0xce,0x3d,0x02,0x01,0x06,0x08,0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07,0x03,0x42,0x00,0x04};
        der.insert(der.end(), blob.begin() + sizeof(BCRYPT_ECCKEY_BLOB), blob.end()); publicKey = Base64(der);
    }
    SigningKey::~SigningKey() { if (value) BCryptDestroyKey(value); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    std::string SigningKey::SignPayload(std::string_view payload, Component component) const
    {
        auto digest = HashBytes(Bytes(payload)); std::array<unsigned char,64> signature{}; ULONG size = 0;
        Require(BCryptSignHash(value, nullptr, digest.data(), ULONG(digest.size()), signature.data(), ULONG(signature.size()), &size, 0) >= 0 && size == 64, "sign local fixture");
        return Serialize(JObject({{"schemaVersion", JNumber(component == Component::Launcher ? 2 : 1)}, {"keyId", JString("test-key")},
            {"payloadBase64", JString(Base64(Bytes(payload)))}, {"signatureBase64", JString(Base64(signature))}})) + "\n";
    }
    std::string SigningKey::Sign(const Feed& feed, bool canonical) const
    {
        auto payload = JObject({{"schemaVersion", JNumber(feed.component == Component::Launcher ? 2 : 1)}, {"productId", JString(ProductId)},
            {"channel", JString("stable")}, {"releaseSequence", JNumber(feed.sequence)}});
        if (feed.component == Component::Launcher) payload.object.emplace_back("version", JString(feed.version));
        payload.object.emplace_back("sourceCommit", JString(feed.commit));
        if (feed.component == Component::Renderer)
        { payload.object.emplace_back("settingsHash", JString(feed.settingsHash)); payload.object.emplace_back("engineVersion", JString(feed.version)); }
        payload.object.emplace_back("artifact", JObject({{"name", JString(feed.component == Component::Launcher ? LauncherName : ArchiveName)},
            {"size", JNumber(int64_t(feed.size))}, {"sha256", JString(feed.hash)}}));
        return SignPayload(Serialize(payload) + (canonical ? "\n" : ""), feed.component);
    }
    void Zip(const fs::path& root, const fs::path& destination, std::optional<std::string> badPath, bool deflate)
    {
        std::string local, central; uint16_t count = 0;
        for (const auto& entry : fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file()) continue;
            auto name = Utf8(entry.path().lexically_relative(root).generic_wstring());
            if (badPath && name == "bin/uvsr-engine.exe") name = *badPath;
            auto raw = ReadFile(entry.path(), MaximumExpandedBytes); std::string compressed = raw;
            if (deflate)
            {
                z_stream stream{}; Require(deflateInit2(&stream, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK, "create deflate fixture");
                compressed.resize(size_t(deflateBound(&stream, uLong(raw.size())))); stream.next_in = reinterpret_cast<Bytef*>(raw.data()); stream.avail_in = uInt(raw.size());
                stream.next_out = reinterpret_cast<Bytef*>(compressed.data()); stream.avail_out = uInt(compressed.size());
                auto result = ::deflate(&stream, Z_FINISH); auto size = stream.total_out; deflateEnd(&stream); Require(result == Z_STREAM_END, "finish fixture compression"); compressed.resize(size);
            }
            auto crc = crc32(0, reinterpret_cast<const Bytef*>(raw.data()), uInt(raw.size())); auto offset = local.size();
            Little(local, 0x04034b50,4); Little(local,20,2); Little(local,0x800,2); Little(local,deflate?8:0,2); Little(local,0,4);
            Little(local,crc,4); Little(local,compressed.size(),4); Little(local,raw.size(),4); Little(local,name.size(),2); Little(local,0,2); local += name; local += compressed;
            Little(central,0x02014b50,4); Little(central,20,2); Little(central,20,2); Little(central,0x800,2); Little(central,deflate?8:0,2); Little(central,0,4);
            Little(central,crc,4); Little(central,compressed.size(),4); Little(central,raw.size(),4); Little(central,name.size(),2); Little(central,0,2); Little(central,0,2);
            Little(central,0,2); Little(central,0,2); Little(central,0,4); Little(central,offset,4); central += name; ++count;
        }
        auto offset = local.size(), length = central.size(); local += central;
        Little(local,0x06054b50,4); Little(local,0,4); Little(local,count,2); Little(local,count,2); Little(local,length,4); Little(local,offset,4); Little(local,0,2);
        WriteAtomic(destination, local);
    }
    Feed MakePackage(const fs::path& root, int64_t sequence, bool legacy)
    {
        CreateDirectories(root / "bin");
        fs::copy_file(legacy ? UVSR_LEGACY_ENGINE_FIXTURE : UVSR_ENGINE_FIXTURE, root / "bin/uvsr-engine.exe", fs::copy_options::overwrite_existing);
        WriteAtomic(root / "bin/D3D12/D3D12Core.dll", "fixture runtime library");
        const auto settings = legacy ? ReadRecord(fs::path(UVSR_TEST_SOURCE) / "tests/fixtures/settings-v11.json") : ParseJson(uvsr::BuildSettingsContractJson());
        WriteRecord(root / "bin/settings/canonical-settings.json", settings);
        for (auto path : runtime_shader_inventory) WriteAtomic(Descendant(root, path), std::string(path));
        for (auto path : runtime_asset_map) WriteAtomic(Descendant(root, path), std::string(path));
        for (auto path : runtime_license_inventory) WriteAtomic(Descendant(root, path), std::string(path));
        if (legacy) for (auto name : {"msaa_visibility_resolve_cs", "pbr_deferred_lighting_msaa_cs", "temporal_aa_blend_cs", "temporal_aa_minimum_cs", "temporal_aa_resolve_cs", "temporal_aa_sharpen_cs"})
            WriteAtomic(root / "bin/shaders/uvsr/dxil" / (std::string(name) + ".bin"), name);
        if (legacy) for (const auto name : {"Andrew-Helmer-Stochastic-Generation-MIT.txt", "Microsoft-DirectX-Graphics-Samples.txt"})
            WriteAtomic(root / "bin/licenses" / name, name);
        Json files; files.kind = Json::Kind::Array;
        for (const auto& entry : fs::recursive_directory_iterator(root)) if (entry.is_regular_file() && entry.path().filename() != PackageName)
            files.array.push_back(JObject({{"relativePath", JString(Utf8(entry.path().lexically_relative(root).generic_wstring()))},
                {"size", JNumber(int64_t(entry.file_size()))}, {"sha256", JString(HashFile(entry.path()))}}));
        auto manifest = JObject({{"schemaVersion", JNumber(1)}, {"productId", JString(ProductId)}, {"production", JBool(true)}, {"configuration", JString("Release")},
            {"releaseSequence", JNumber(sequence)}, {"sourceCommit", JString(Commit)}, {"settingsHash", Member(settings,"settingsHash")}, {"engineVersion", Member(settings,"engineVersion")},
            {"executableSha256", JString(HashFile(root / "bin/uvsr-engine.exe"))}, {"files", files}});
        WriteRecord(root / PackageName, manifest);
        return {Component::Renderer, sequence, Commit, Text(settings,"engineVersion"), Text(settings,"settingsHash"), std::string(64,'a'), 1};
    }
    Json InstallOldLauncher(const Paths& paths, std::string_view owner, bool oldName)
    {
        const auto hash = HashFile(UVSR_LEGACY_LAUNCHER_FIXTURE); auto root = paths.Launcher(hash); CreateDirectories(root);
        fs::copy_file(UVSR_LEGACY_LAUNCHER_FIXTURE, root / (oldName ? "UVSR Launcher.exe" : LauncherName));
        auto marker = JObject({{"schemaVersion",JNumber(1)}, {"productId",JString(ProductId)}, {"installationId",JString(std::string(owner))}, {"releaseSequence",JNumber(16)},
            {"version",JString("1.2.0")}, {"executableSha256",JString(hash)}, {"executableSize",JNumber(int64_t(fs::file_size(UVSR_LEGACY_LAUNCHER_FIXTURE)))}, {"installedUtc",JString(UtcNow())}});
        WriteRecord(root / LauncherPackageName, marker); auto state = LauncherStateFromMarker(marker, true); WriteRecord(paths.state / "launcher-state.json", state); return state;
    }
    Fixture::Fixture() : root(fs::temp_directory_path() / ("uvsr-native-contract-" + Guid())), paths(Paths::Create(root / "local", root / "desktop", root / "programs")),
        registry(L"Software\\UVSR Native Contract Tests\\" + Wide(Guid())), suffix(Guid())
    {
        CreateDirectories(root); services.platform = [] {}; services.executable = UVSR_LAUNCHER_FIXTURE;
        services.health = [](const fs::path&, int64_t, std::string_view, std::stop_token) { return 0; };
        services.processes = [](const fs::path&, Component, bool) { return Processes{}; };
        services.start = [](const fs::path&, std::span<const std::wstring>, bool) {};
        launcher = {Component::Launcher, 18, Commit, "1.4.0", {}, HashFile(UVSR_NEWER_LAUNCHER_FIXTURE), fs::file_size(UVSR_NEWER_LAUNCHER_FIXTURE)};
        services.download = [this](std::string_view url, const fs::path& destination, uint64_t, std::optional<std::string_view>, std::stop_token stop, const Report&)
        {
            CheckCancelled(stop); Require(!failDownload, "injected download failure");
            if (url == RendererFeedUrl) WriteAtomic(destination, signing.Sign(renderer));
            else if (url == LauncherFeedUrl) WriteAtomic(destination, signing.Sign(launcher));
            else { CreateDirectories(destination.parent_path()); fs::copy_file(url.ends_with(LauncherName) ? fs::path(UVSR_NEWER_LAUNCHER_FIXTURE) : archive, destination, fs::copy_options::overwrite_existing); }
        };
    }
    Fixture::~Fixture()
    {
        if (shellUsed) RegDeleteTreeW(HKEY_CURRENT_USER, registry.c_str());
        std::error_code error;
        if (IsDescendant(root, fs::temp_directory_path()) && root.filename().wstring().starts_with(L"uvsr-native-contract-")) fs::remove_all(root, error);
    }
    Installer Fixture::Make() { shellUsed = true; return Installer(paths, services, signing.publicKey, "test-key", registry, suffix); }
    void Fixture::Package(int64_t sequence)
    { auto package = root / ("package-" + std::to_string(sequence)); renderer = MakePackage(package, sequence); archive = root / ("package-" + std::to_string(sequence) + ".zip"); Zip(package, archive); renderer.size = fs::file_size(archive); renderer.hash = HashFile(archive); }
}
