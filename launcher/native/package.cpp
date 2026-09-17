#include "core.h"
#include "runtime_inventory.h"
#include "settings_snapshot.h"
#include "engine_identity.h"
#include <algorithm>
#include <map>

namespace uvsr::launcher
{
    namespace
    {
        // installed schema 11 remains verifiable for upgrade, rollback and uninstall only.
        constexpr std::string_view LegacyShaders[] = {
            "bin/shaders/uvsr/dxil/msaa_visibility_resolve_cs.bin",
            "bin/shaders/uvsr/dxil/pbr_deferred_lighting_msaa_cs.bin",
            "bin/shaders/uvsr/dxil/temporal_aa_blend_cs.bin",
            "bin/shaders/uvsr/dxil/temporal_aa_minimum_cs.bin",
            "bin/shaders/uvsr/dxil/temporal_aa_resolve_cs.bin",
            "bin/shaders/uvsr/dxil/temporal_aa_sharpen_cs.bin"};
        constexpr std::string_view PublishedR16OnlyPaths[] = {
            "bin/shaders/framework/dxil/blit_ps.bin",
            "bin/shaders/framework/dxil/fullscreen_vs.bin",
            "bin/shaders/framework/dxil/imgui_pixel.bin",
            "bin/shaders/framework/dxil/imgui_vertex.bin",
            "bin/shaders/framework/dxil/passes/depth_ps.bin",
            "bin/shaders/framework/dxil/passes/depth_vs_buffer_loads.bin",
            "bin/shaders/framework/dxil/rect_vs.bin",
            "bin/shaders/framework/dxil/sharpen_ps.bin",
            "bin/shaders/framework/dxil/skinning_cs.bin",
            "bin/licenses/JsonCpp-Public-Domain-or-MIT.txt"};
        bool PublishedR16Path(std::string_view path, bool directory = false)
        {
            for (const auto known : PublishedR16OnlyPaths)
                if ((!directory && path == known) || (directory && known.starts_with(path) &&
                    known.size() > path.size() && known[path.size()] == '/')) return true;
            return false;
        }
        bool HasSha256(std::string_view bytes, std::string_view expected)
        {
            const auto digest = HashBytes({reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()});
            std::string hash;
            for (const auto byte : digest) { hash += "0123456789abcdef"[byte >> 4]; hash += "0123456789abcdef"[byte & 15]; }
            return HashEqual(hash, expected);
        }
        bool InInventory(std::string_view path)
        {
            return std::ranges::find(runtime_shader_inventory, path) != std::end(runtime_shader_inventory) ||
                std::ranges::find(runtime_license_inventory, path) != std::end(runtime_license_inventory) ||
                std::ranges::find(runtime_asset_map, path) != std::end(runtime_asset_map);
        }
        const std::set<std::string>& Directories()
        {
            static const auto result = []
            {
                std::set<std::string> directories{"bin", "bin/D3D12", "bin/settings", "bin/licenses"};
                const auto add = [&](auto& inventory)
                {
                    for (auto path : inventory)
                    {
                        auto slash = path.rfind('/');
                        while (slash != path.npos)
                        { path = path.substr(0, slash); directories.emplace(path); slash = path.rfind('/'); }
                    }
                };
                add(runtime_shader_inventory); add(runtime_asset_map); return directories;
            }();
            return result;
        }
        bool EqualJson(const Json& a, const Json& b)
        { return json::Equal(a.Root(), b.Root()); }
        bool CurrentPackagePath(std::string_view path, bool directory = false)
        {
            try { ValidateRelativePath(path); } catch (...) { return false; }
            if (directory) return Directories().contains(std::string(path)) || path.starts_with("bin/licenses/");
            auto name = path.substr(path.rfind('/') == path.npos ? 0 : path.rfind('/') + 1);
            auto lower = Lower(std::string(name));
            if (lower.starts_with(".git")) return false;
            auto dot = lower.rfind('.');
            if (dot != lower.npos)
            {
                const auto extension = lower.substr(dot);
                for (auto forbidden : {".py", ".pyc", ".ps1", ".cmd", ".bat", ".cmake", ".cpp", ".cxx", ".cc", ".h", ".hpp", ".hlsl", ".hlsli", ".pdb", ".ilk", ".lib", ".exp", ".obj", ".sln", ".vcxproj"})
                    if (extension == forbidden) return false;
            }
            return path == "bin/uvsr-engine.exe" || path == "bin/D3D12/D3D12Core.dll" ||
                path == "bin/settings/canonical-settings.json" || InInventory(path);
        }
    }
    bool AllowedPackagePath(std::string_view path, bool directory)
    { return CurrentPackagePath(path, directory) || PublishedR16Path(path, directory); }
    Package ValidatePackage(const fs::path& root, const Feed* feed)
    {
        RejectReparseChain(root);
        Package package;
        const auto manifestPath = root / PackageName;
        RejectReparseChain(manifestPath);
        const auto manifestBytes = ReadFile(manifestPath, 16u << 20);
        package.manifest = json::Parse(manifestBytes, 32);
        // the immutable r16 manifest pins its old inventory and every file hash during upgrade or repair.
        const bool publishedR16 = HasSha256(manifestBytes, "1a365ca97db2ee2c89b0b28bb5441b6b00212e89dc45bfc23a82ddddca4df9c0");
        const auto& manifest = package.manifest;
        RequireExactObject(manifest, {"schemaVersion", "productId", "production", "configuration", "releaseSequence", "sourceCommit", "settingsHash", "engineVersion", "executableSha256", "files"}, "package manifest");
        Require(Number(manifest, "schemaVersion") == 1 && Text(manifest, "productId") == ProductId && Flag(manifest, "production") &&
            Text(manifest, "configuration") == "Release" && Number(manifest, "releaseSequence") > 0 && Number(manifest, "releaseSequence") <= MaximumSequence &&
            IsLowerHex(Text(manifest, "sourceCommit"), 40) && IsLowerHex(Text(manifest, "settingsHash"), 32) &&
            IsLowerHex(Text(manifest, "executableSha256"), 64) && IsCanonicalDottedVersion(Text(manifest, "engineVersion"), 4, 65535), "The renderer manifest identity is invalid.");
        if (feed)
            Require(feed->component == Component::Renderer && Number(manifest, "releaseSequence") == feed->sequence && Text(manifest, "sourceCommit") == feed->commit &&
                Text(manifest, "settingsHash") == feed->settingsHash && Text(manifest, "engineVersion") == feed->version, "The renderer manifest does not match its signed feed.");
        const bool legacy = !feed && Text(manifest, "settingsHash") == "df2b9ab2a2c8d65e4415dc93c0051f0c";
        const auto& files = Member(manifest, "files");
        Require(files.Type() == json::Kind::Array && files.Count() > 0 && files.Count() <= 100000, "The renderer file inventory is invalid.");
        std::map<std::string, PackageFile> recorded;
        uint64_t expanded = 0;
        for (auto item = files.First(); item.IsValid(); item = item.Next())
        {
            RequireExactObject(item, {"relativePath", "size", "sha256"}, "package file");
            auto size = Number(item, "size");
            PackageFile file{std::string(Text(item, "relativePath")), std::string(Text(item, "sha256")), uint64_t(size)};
            Require(size >= 0 && uint64_t(size) <= MaximumExpandedBytes - expanded && IsLowerHex(file.hash, 64) &&
                (CurrentPackagePath(file.path) || (publishedR16 && PublishedR16Path(file.path)) ||
                    (legacy && (std::ranges::find(LegacyShaders, file.path) != std::end(LegacyShaders) ||
                    file.path == "bin/licenses/Andrew-Helmer-Stochastic-Generation-MIT.txt" || file.path == "bin/licenses/Microsoft-DirectX-Graphics-Samples.txt"))) &&
                recorded.emplace(Lower(file.path), file).second, "The renderer file inventory contains an unsafe or duplicate member.");
            expanded += uint64_t(size); package.files.push_back(file);
        }
        for (auto required : {"bin/uvsr-engine.exe", "bin/D3D12/D3D12Core.dll", "bin/settings/canonical-settings.json"})
            Require(recorded.contains(Lower(required)), "The renderer package is incomplete.");
        if (!publishedR16)
        {
            for (const auto& required : runtime_shader_inventory)
                Require(recorded.contains(Lower(std::string(required))), "A required runtime shader is missing.");
            for (const auto required : runtime_license_inventory)
                Require(recorded.contains(Lower(std::string(required))), "A required runtime license is missing.");
        }
        for (const auto& required : runtime_asset_map)
            Require(recorded.contains(Lower(std::string(required))), "A retained runtime asset or notice is missing.");
        if (legacy)
        {
            for (const auto required : LegacyShaders)
                Require(recorded.contains(std::string(required)), "The installed schema 11 inventory is incomplete.");
            for (const auto required : {"bin/licenses/Andrew-Helmer-Stochastic-Generation-MIT.txt", "bin/licenses/Microsoft-DirectX-Graphics-Samples.txt"})
                Require(recorded.contains(Lower(required)), "An installed schema 11 license is missing.");
        }
        std::set<std::string> actual;
        for (const auto& item : fs::recursive_directory_iterator(root))
        {
            RejectReparseChain(item.path());
            const auto relative = Utf8(item.path().lexically_relative(root).generic_wstring());
            if (item.is_directory())
            {
                Require(CurrentPackagePath(relative, true) || (publishedR16 && PublishedR16Path(relative, true)),
                    "The package has an unexpected directory.");
                continue;
            }
            Require(item.is_regular_file(), "The package has a non-file member.");
            if (relative == PackageName) continue;
            auto found = recorded.find(Lower(relative));
            Require(found != recorded.end() && found->second.path == relative && actual.emplace(Lower(relative)).second, "The renderer package inventory changed.");
            VerifyFile(item.path(), found->second.size, found->second.hash);
        }
        Require(actual.size() == recorded.size(), "A recorded renderer file is missing.");
        Require(HashEqual(recorded.at("bin/uvsr-engine.exe").hash, Text(manifest, "executableSha256")), "The executable hash does not match its manifest.");
        const auto engine = root / "bin/uvsr-engine.exe";
        ValidatePe(engine);
        for (const auto& [key, expected] : std::initializer_list<std::pair<std::wstring_view, std::string>>{
            {L"ProductName", "UVSR Engine"}, {L"FileDescription", "UVSR Engine"}, {L"InternalName", "uvsr-engine"}, {L"OriginalFilename", EngineName},
            {L"FileVersion", std::string(Text(manifest, "engineVersion"))}, {L"ProductVersion", std::string(Text(manifest, "engineVersion")) + "+" + std::string(Text(manifest, "settingsHash"))},
            {L"SourceCommit", std::string(Text(manifest, "sourceCommit"))}, {L"SourceIdentity", std::string(Text(manifest, "sourceCommit"))},
            {L"SettingsNumberHash", std::string(Text(manifest, "settingsHash"))}, {L"BuildConfiguration", "Release"}, {L"ProductionBuild", "true"}})
            Require(PeString(engine, key) == expected, "The renderer PE identity does not match its package.");
        const auto settings = ReadRecord(root / "bin/settings/canonical-settings.json", 16u << 20);
        const auto encodedAuthority = BuildSettingsContractJson();
        Require(encodedAuthority.IsValid(), encodedAuthority.Failure().message);
        const auto authority = ParseJson(std::string_view{encodedAuthority.Data(), encodedAuthority.Size()});
        bool settingsValid = EqualJson(settings, authority);
        if (legacy)
        {
            settingsValid = HasSha256(Serialize(settings), "3fb8bc72cbe4e9606185c8ab17def86fd40079f097f9f3f9cadb27531798b1b3");
        }
        Require(settingsValid && Text(settings, "settingsHash") == Text(manifest, "settingsHash") &&
            Text(settings, "engineVersion") == Text(manifest, "engineVersion"), "The canonical settings contract does not match the renderer and C++ authority.");
        return package;
    }
}
