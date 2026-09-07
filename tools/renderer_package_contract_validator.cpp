#include "strict_json_contract.h"
#include "pe_image.h"

#include "engine_identity.h"
#include "sha256.h"
#include "settings_snapshot.h"
#include "settings_snapshot_schema.h"
#include "ui_settings_command_catalog.h"

#include <Windows.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace uvsr::contract;

    constexpr std::string_view ProductId =
        "0c47a7a8-1ec4-4ffd-b6c4-2f7614181223";
    constexpr std::int64_t MaximumReleaseSequence = 9007199254740991ll;
    constexpr std::uintmax_t RequiredD3D12CoreSize = 5'027'640u;
    constexpr std::string_view RequiredD3D12CoreSha256 =
        "eddf4cff4eda8162624b88694ad2adf4b09bc5aee6339191f39adf8ae48b41e7";

    struct FileRecord
    {
        std::string path;
        std::uintmax_t size = 0u;
        std::string sha256;
    };

    struct Manifest
    {
        std::int64_t releaseSequence = 0;
        std::string configuration;
        std::string sourceCommit;
        std::string settingsHash;
        std::string engineVersion;
        std::string executableSha256;
        std::vector<FileRecord> files;
    };

    using ShaderInventory = std::set<std::string>;
    using ProtectedRuntimeInventory = std::set<std::string>;
    using RequiredFileInventory = std::vector<FileRecord>;
    constexpr std::array<std::string_view, 4> BasePackagePaths = {
        "bin/D3D12/D3D12Core.dll",
        "bin/licenses/Intel-XeGTAO-MIT.txt",
        "bin/settings/canonical-settings.json",
        "bin/uvsr-engine.exe"
    };

    [[nodiscard]] const RequiredFileInventory&
        ProductionRequiredLicenses()
    {
        static const RequiredFileInventory Licenses = {
            {
                "bin/licenses/Intel-XeGTAO-MIT.txt",
                1081u,
                "1f3bfd6b628535f0f0e779e70c260cfdc1bb45bc8644d7108e65f83e24a05cba"
            }
        };
        return Licenses;
    }

    [[nodiscard]] std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return value;
    }

    [[nodiscard]] bool StartsWith(
        std::string_view value,
        std::string_view prefix)
    {
        return value.size() >= prefix.size() &&
            value.compare(0u, prefix.size(), prefix) == 0;
    }

    [[nodiscard]] bool HasForbiddenExtension(std::string_view path)
    {
        const std::size_t slash = path.find_last_of('/');
        const std::string name = Lower(std::string(path.substr(
            slash == std::string_view::npos ? 0u : slash + 1u)));
        if (StartsWith(name, ".git"))
            return true;
        const std::size_t dot = name.find_last_of('.');
        const std::string extension = dot == std::string::npos
            ? std::string() : name.substr(dot);
        static const std::set<std::string> Forbidden = {
            ".py", ".pyc", ".ps1", ".cmd", ".bat", ".cmake",
            ".cpp", ".cxx", ".cc", ".h", ".hpp", ".hlsl", ".hlsli",
            ".pdb", ".ilk", ".lib", ".exp", ".obj", ".sln", ".vcxproj"
        };
        return Forbidden.count(extension) != 0u;
    }

    [[nodiscard]] bool EndsWith(
        std::string_view value,
        std::string_view suffix)
    {
        return value.size() >= suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(),
                suffix) == 0;
    }

    [[nodiscard]] bool IsAllowedMediaFile(std::string_view path)
    {
        if (StartsWith(path, "media/luts/kodak/"))
            return EndsWith(path, ".cube");
        if (StartsWith(path, "media/environments/"))
            return EndsWith(path, ".hdr");
        if (StartsWith(path, "media/uvsr/noise/"))
        {
            return path == "media/uvsr/noise/manifest.json" ||
                EndsWith(path, ".bin");
        }
        if (StartsWith(path,
                "media/glTF-Sample-Assets/Models/"))
        {
            return EndsWith(path, ".scene.json") ||
                EndsWith(path, ".gltf") || EndsWith(path, ".glb") ||
                EndsWith(path, ".bin") || EndsWith(path, ".png");
        }
        return false;
    }

    [[nodiscard]] bool IsAllowedFile(std::string_view path)
    {
        if (path.empty() || path.front() == '/' || path.back() == '/' ||
            path.find('\\') != std::string_view::npos ||
            path.find(':') != std::string_view::npos ||
            path.find("//") != std::string_view::npos ||
            path == "." || path == ".." || StartsWith(path, "./") ||
            path.find("/../") != std::string_view::npos ||
            path.find("/./") != std::string_view::npos ||
            path.size() >= 3u && path.compare(path.size() - 3u, 3u, "/..") == 0 ||
            path.size() >= 2u && path.compare(path.size() - 2u, 2u, "/.") == 0 ||
            HasForbiddenExtension(path))
        {
            return false;
        }
        return path == "bin/uvsr-engine.exe" ||
            path == "bin/settings/canonical-settings.json" ||
            path == "bin/D3D12/D3D12Core.dll" ||
            (StartsWith(path, "bin/shaders/") &&
                EndsWith(path, ".bin")) ||
            (StartsWith(path, "bin/licenses/") && path.size() > 13u) ||
            IsAllowedMediaFile(path);
    }

    [[nodiscard]] bool IsAllowedDirectory(std::string_view path)
    {
        return path == "bin" || path == "media" ||
            path == "bin/D3D12" || path == "bin/shaders" ||
            path == "bin/licenses" || path == "bin/settings" ||
            StartsWith(path, "bin/shaders/") ||
            StartsWith(path, "bin/licenses/") || StartsWith(path, "media/");
    }

    [[nodiscard]] ShaderInventory LoadShaderInventory(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot read runtime shader inventory");
        ShaderInventory result;
        std::string previous;
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || !StartsWith(line, "bin/shaders/") ||
                !EndsWith(line, ".bin") || !IsAllowedFile(line) ||
                (!previous.empty() && line <= previous) ||
                !result.insert(line).second)
            {
                throw std::runtime_error(
                    "runtime shader inventory is not canonical");
            }
            previous = line;
        }
        if (stream.bad() || result.size() != 33u)
            throw std::runtime_error("runtime shader inventory is incomplete");
        return result;
    }

    [[nodiscard]] bool IsSafeRepositoryPath(std::string_view path)
    {
        if (path.empty() || path.front() == '/' || path.back() == '/' ||
            path.find('\\') != std::string_view::npos ||
            path.find(':') != std::string_view::npos ||
            path.find('|') != std::string_view::npos ||
            path.find("//") != std::string_view::npos || path == "." ||
            path == ".." || StartsWith(path, "./") ||
            path.find("/../") != std::string_view::npos ||
            path.find("/./") != std::string_view::npos ||
            EndsWith(path, "/..") || EndsWith(path, "/."))
        {
            return false;
        }
        return StartsWith(path, "assets/") ||
            StartsWith(path, "legal/documentation/");
    }

    [[nodiscard]] ProtectedRuntimeInventory LoadRuntimeAssetMap(
        const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot read runtime asset map");
        ProtectedRuntimeInventory result;
        std::string previous;
        std::string line;
        std::size_t notices = 0u;
        std::size_t environments = 0u;
        std::size_t bistro = 0u;
        std::size_t sanMiguel = 0u;
        std::size_t noise = 0u;
        std::size_t luts = 0u;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                throw std::runtime_error(
                    "runtime asset map must use LF line endings");
            const std::size_t separator = line.find('|');
            if (separator == std::string::npos || separator == 0u ||
                line.find('|', separator + 1u) != std::string::npos)
            {
                throw std::runtime_error("runtime asset map is malformed");
            }
            const std::string packagePath = line.substr(0u, separator);
            const std::string sourcePath = line.substr(separator + 1u);
            if (!IsAllowedFile(packagePath) ||
                (!StartsWith(packagePath, "media/") &&
                    !StartsWith(packagePath, "bin/licenses/")) ||
                !IsSafeRepositoryPath(sourcePath) ||
                (!previous.empty() && packagePath <= previous) ||
                !result.insert(packagePath).second)
            {
                throw std::runtime_error(
                    "runtime asset map is not canonical");
            }
            notices += StartsWith(packagePath, "bin/licenses/") ? 1u : 0u;
            environments += StartsWith(
                packagePath, "media/environments/") ? 1u : 0u;
            bistro += StartsWith(packagePath,
                "media/glTF-Sample-Assets/Models/bistro_interior_retextured/")
                ? 1u : 0u;
            sanMiguel += StartsWith(packagePath,
                "media/glTF-Sample-Assets/Models/san_miguel_retextured/")
                ? 1u : 0u;
            noise += StartsWith(
                packagePath, "media/uvsr/noise/") ? 1u : 0u;
            luts += StartsWith(packagePath, "media/luts/kodak/") ? 1u : 0u;
            previous = packagePath;
        }
        if (stream.bad() || result.size() != 309u || notices != 4u ||
            environments != 6u || bistro != 7u ||
            sanMiguel != 276u || noise != 13u || luts != 3u)
        {
            throw std::runtime_error("runtime asset map is incomplete");
        }
        return result;
    }

    [[nodiscard]] bool EqualContractValue(const JsonValue& left, const JsonValue& right)
    {
        if (left.kind != right.kind || left.object.size() != right.object.size() ||
            left.array.size() != right.array.size())
            return false;
        switch (left.kind)
        {
        case JsonValue::Kind::Number: return Integer(left, "value") == Integer(right, "value");
        case JsonValue::Kind::String: return left.string == right.string;
        case JsonValue::Kind::Boolean: return left.boolean == right.boolean;
        default: break;
        }
        for (std::size_t index = 0; index < left.array.size(); ++index)
            if (!EqualContractValue(left.array[index], right.array[index]))
                return false;
        for (const auto& member : left.object)
        {
            const JsonValue* expected = right.Find(member.first);
            if (!expected || !EqualContractValue(member.second, *expected))
                return false;
        }
        return true;
    }

    void ValidateSettingsContract(const fs::path& path, const Manifest& manifest)
    {
        const JsonValue actual = ParseJson(ReadFile(path, 1024u * 1024u));
        const JsonValue expected = ParseJson(uvsr::BuildSettingsContractJson());
        if (!EqualContractValue(actual, expected) ||
            String(Member(actual, "settingsHash"), "settings hash") != manifest.settingsHash ||
            String(Member(actual, "engineVersion"), "engine version") != manifest.engineVersion)
        {
            throw std::runtime_error("canonical settings differ from the compiled catalog or manifest");
        }
    }

    void RejectReparsePoint(const fs::path& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("cannot inspect " + path.string());
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u)
            throw std::runtime_error("package reparse points are forbidden");
    }

    [[nodiscard]] std::string Sha256(const fs::path& path)
    {
        try
        {
            return uvsr::Sha256File(path);
        }
        catch (const uvsr::Sha256Error& error)
        {
            if (error.Stage() == uvsr::Sha256Stage::OpenFile)
            {
                throw std::runtime_error("cannot hash " + path.string());
            }
            throw std::runtime_error("Windows SHA-256 operation failed");
        }
    }

    [[nodiscard]] Manifest ParseManifest(std::string_view text)
    {
        const JsonValue root = ParseJson(text);
        RequireExactObject(root,
            { "schemaVersion", "productId", "production", "releaseSequence",
              "configuration", "sourceCommit", "settingsHash", "engineVersion",
              "executableSha256", "files" },
            "renderer package manifest");
        if (Integer(Member(root, "schemaVersion"), "manifest schema") != 1 ||
            String(Member(root, "productId"), "product ID") != ProductId ||
            !Boolean(Member(root, "production"), "production marker"))
        {
            throw std::runtime_error("manifest identity is not canonical");
        }
        Manifest result;
        result.releaseSequence = Integer(Member(root, "releaseSequence"),
            "release sequence");
        result.configuration = String(Member(root, "configuration"),
            "build configuration");
        result.sourceCommit = String(Member(root, "sourceCommit"),
            "source commit");
        result.settingsHash = String(Member(root, "settingsHash"),
            "settings hash");
        result.engineVersion = String(Member(root, "engineVersion"),
            "engine version");
        result.executableSha256 = String(Member(root, "executableSha256"),
            "executable SHA-256");
        if (result.releaseSequence < 1 ||
            result.releaseSequence > MaximumReleaseSequence ||
            result.configuration != "Release" ||
            !IsLowerHex(result.sourceCommit, 40u) ||
            result.settingsHash != uvsr::GetSettingsNumberHashText() ||
            !IsCanonicalDottedVersion(result.engineVersion, 4u, 65535) ||
            !IsLowerHex(result.executableSha256, 64u))
        {
            throw std::runtime_error("manifest values are not canonical");
        }
        const JsonValue& files = Member(root, "files");
        if (files.kind != JsonValue::Kind::Array || files.array.empty() ||
            files.array.size() > 100000u)
        {
            throw std::runtime_error("manifest file count is outside its limit");
        }
        std::set<std::string> insensitivePaths;
        std::string previous;
        for (const JsonValue& value : files.array)
        {
            RequireExactObject(value, { "relativePath", "size", "sha256" },
                "manifest file");
            FileRecord file;
            file.path = String(Member(value, "relativePath"), "relative path");
            const std::int64_t size = Integer(Member(value, "size"), "file size");
            file.sha256 = String(Member(value, "sha256"), "file SHA-256");
            if (!IsAllowedFile(file.path) || size < 0 ||
                !IsLowerHex(file.sha256, 64u) ||
                (!previous.empty() && file.path <= previous) ||
                !insensitivePaths.insert(Lower(file.path)).second)
            {
                throw std::runtime_error("manifest file inventory is not canonical");
            }
            file.size = static_cast<std::uintmax_t>(size);
            previous = file.path;
            result.files.push_back(std::move(file));
        }
        return result;
    }

    [[nodiscard]] std::wstring VersionString(
        const std::vector<unsigned char>& data,
        WORD language,
        WORD codePage,
        std::wstring_view name)
    {
        wchar_t key[128]{};
        _snwprintf_s(key, _TRUNCATE, L"\\StringFileInfo\\%04x%04x\\%s",
            language, codePage, std::wstring(name).c_str());
        void* value = nullptr;
        UINT size = 0u;
        if (!VerQueryValueW(data.data(), key, &value, &size) ||
            value == nullptr || size < 2u)
        {
            throw std::runtime_error("engine version resource is incomplete");
        }
        return std::wstring(static_cast<const wchar_t*>(value), size - 1u);
    }

    using uvsr::PeImage;

    [[nodiscard]] bool IsForbiddenRuntimeImport(std::string name)
    {
        name = Lower(std::move(name));
        return StartsWith(name, "vcruntime") ||
            StartsWith(name, "msvcp") || StartsWith(name, "concrt") ||
            StartsWith(name, "ucrtbase") ||
            StartsWith(name, "api-ms-win-crt-") ||
            name == "d3d12sdklayers.dll";
    }

    void ValidateImportDirectory(const PeImage& image)
    {
        const IMAGE_DATA_DIRECTORY& directory =
            image.optionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0u || directory.Size == 0u)
            throw std::runtime_error("engine PE import directory is missing");
        const std::size_t maximum =
            directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
        if (maximum == 0u || maximum > 4096u)
            throw std::runtime_error("engine PE import directory is invalid");
        const std::size_t offset = image.FileOffset(
            directory.VirtualAddress, directory.Size, "import directory");
        bool terminated = false;
        for (std::size_t index = 0u; index < maximum; ++index)
        {
            const IMAGE_IMPORT_DESCRIPTOR entry =
                image.ReadAt<IMAGE_IMPORT_DESCRIPTOR>(offset + index * sizeof(entry), "import descriptor");
            if (entry.Name == 0u && entry.FirstThunk == 0u &&
                entry.OriginalFirstThunk == 0u)
            {
                terminated = true;
                break;
            }
            if (entry.Name == 0u)
                throw std::runtime_error("engine PE import name is missing");
            const std::string name = image.ReadString(entry.Name, 260);
            if (name.empty())
                throw std::runtime_error("engine PE import name is empty");
            if (IsForbiddenRuntimeImport(name))
            {
                throw std::runtime_error(
                    "engine imports forbidden runtime " + name);
            }
        }
        if (!terminated)
            throw std::runtime_error("engine PE import directory is unterminated");
    }

    void ValidateDebugDirectory(const PeImage& image)
    {
        if (image.optionalHeader.NumberOfRvaAndSizes <=
            IMAGE_DIRECTORY_ENTRY_DEBUG)
        {
            return;
        }
        const IMAGE_DATA_DIRECTORY& directory =
            image.optionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        if (directory.VirtualAddress == 0u || directory.Size == 0u)
            return;
        if (directory.Size % sizeof(IMAGE_DEBUG_DIRECTORY) != 0u ||
            directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY) > 128u)
        {
            throw std::runtime_error("engine PE debug directory is invalid");
        }
        const std::size_t offset = image.FileOffset(
            directory.VirtualAddress, directory.Size, "debug directory");
        const std::size_t count =
            directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
        for (std::size_t index = 0u; index < count; ++index)
        {
            const IMAGE_DEBUG_DIRECTORY entry =
                image.ReadAt<IMAGE_DEBUG_DIRECTORY>(offset + index * sizeof(entry), "debug entry");
            if (entry.Type == IMAGE_DEBUG_TYPE_CODEVIEW || entry.Type == 17u)
            {
                throw std::runtime_error(
                    "engine contains embedded symbol information");
            }
        }
    }

    void ValidateEnginePeContract(const fs::path& engine)
    {
        const PeImage image(engine);
        if (image.fileHeader.PointerToSymbolTable != 0u || image.fileHeader.NumberOfSymbols != 0u ||
            image.fileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
            image.optionalHeader.Subsystem != IMAGE_SUBSYSTEM_WINDOWS_GUI ||
            image.optionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
            throw std::runtime_error("engine PE architecture or symbol contract is invalid");
        for (const auto& section : image.sections)
        {
            std::array<char, IMAGE_SIZEOF_SHORT_NAME + 1u> name{};
            std::memcpy(name.data(), section.Name, IMAGE_SIZEOF_SHORT_NAME);
            if (StartsWith(Lower(name.data()), ".debug"))
                throw std::runtime_error("engine contains a debug section");
        }
        ValidateImportDirectory(image);
        ValidateDebugDirectory(image);
    }

    void ValidateEngineMetadata(const fs::path& engine, const Manifest& manifest)
    {
        ValidateEnginePeContract(engine);
        DWORD ignored = 0u;
        const DWORD size = GetFileVersionInfoSizeW(engine.c_str(), &ignored);
        if (size == 0u)
            throw std::runtime_error("engine has no version resource");
        std::vector<unsigned char> data(size);
        if (!GetFileVersionInfoW(engine.c_str(), 0u, size, data.data()))
            throw std::runtime_error("cannot read engine version resource");
        struct Translation { WORD language; WORD codePage; };
        Translation* translations = nullptr;
        UINT translationBytes = 0u;
        if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                reinterpret_cast<void**>(&translations), &translationBytes) ||
            translations == nullptr || translationBytes < sizeof(Translation))
        {
            throw std::runtime_error("engine version translation is missing");
        }
        const Translation translation = translations[0];
        const auto widen = [](const std::string& value)
        {
            return std::wstring(value.begin(), value.end());
        };
        if (VersionString(data, translation.language, translation.codePage,
                L"ProductName") != L"UVSR Engine" ||
            VersionString(data, translation.language, translation.codePage,
                L"FileDescription") != L"UVSR Engine" ||
            VersionString(data, translation.language, translation.codePage,
                L"InternalName") != L"uvsr-engine" ||
            VersionString(data, translation.language, translation.codePage,
                L"OriginalFilename") != L"uvsr-engine.exe" ||
            VersionString(data, translation.language, translation.codePage,
                L"FileVersion") != widen(manifest.engineVersion) ||
            VersionString(data, translation.language, translation.codePage,
                L"ProductVersion") != widen(
                    manifest.engineVersion + "+" + manifest.settingsHash) ||
            VersionString(data, translation.language, translation.codePage,
                L"BuildConfiguration") != L"Release" ||
            VersionString(data, translation.language, translation.codePage,
                L"ProductionBuild") != L"true" ||
            VersionString(data, translation.language, translation.codePage,
                L"SourceCommit") != widen(manifest.sourceCommit) ||
            VersionString(data, translation.language, translation.codePage,
                L"SourceIdentity") != widen(manifest.sourceCommit) ||
            VersionString(data, translation.language, translation.codePage,
                L"SettingsNumberHash") != widen(manifest.settingsHash))
        {
            throw std::runtime_error("engine metadata does not match the manifest");
        }
    }

    void ValidatePackage(
        const fs::path& requestedRoot,
        bool verifyMetadata,
        const ShaderInventory& expectedShaders,
        const ProtectedRuntimeInventory& expectedProtectedRuntime,
        const RequiredFileInventory& requiredLicenses =
            ProductionRequiredLicenses())
    {
        const fs::path root = fs::weakly_canonical(requestedRoot);
        if (!fs::is_directory(root))
            throw std::runtime_error("package root is missing");
        RejectReparsePoint(root);
        const fs::path manifestPath = root / "package-manifest.json";
        RejectReparsePoint(manifestPath);
        const Manifest manifest = ParseManifest(ReadFile(
            manifestPath, 16u * 1024u * 1024u));

        std::map<std::string, FileRecord> actual;
        ShaderInventory actualShaders;
        ProtectedRuntimeInventory actualProtectedRuntime;
        for (const fs::directory_entry& entry :
            fs::recursive_directory_iterator(root))
        {
            RejectReparsePoint(entry.path());
            const std::string relative = fs::relative(entry.path(), root)
                .generic_string();
            if (entry.is_directory())
            {
                if (!IsAllowedDirectory(relative))
                    throw std::runtime_error("unexpected package directory " + relative);
                continue;
            }
            if (!entry.is_regular_file())
                throw std::runtime_error("package contains a non-file entry");
            if (relative == "package-manifest.json")
                continue;
            if (!IsAllowedFile(relative))
                throw std::runtime_error("unexpected package file " + relative);
            FileRecord file{ relative, entry.file_size(), Sha256(entry.path()) };
            if (!actual.emplace(Lower(relative), std::move(file)).second)
                throw std::runtime_error("case-insensitive package path collision");
            if (StartsWith(relative, "bin/shaders/"))
                actualShaders.insert(relative);
            if (StartsWith(relative, "media/") || expectedProtectedRuntime.count(relative))
                actualProtectedRuntime.insert(relative);
        }
        if (actual.size() != manifest.files.size())
            throw std::runtime_error("package file count does not match manifest");

        for (const FileRecord& required : requiredLicenses)
        {
            const auto found = actual.find(Lower(required.path));
            if (found == actual.end() || found->second.path != required.path ||
                found->second.size != required.size ||
                found->second.sha256 != required.sha256)
            {
                throw std::runtime_error(
                    "required packaged notice differs: " + required.path);
            }
        }

        for (const FileRecord& expected : manifest.files)
        {
            const auto found = actual.find(Lower(expected.path));
            if (found == actual.end() || found->second.path != expected.path ||
                found->second.size != expected.size ||
                found->second.sha256 != expected.sha256)
            {
                throw std::runtime_error(
                    "package file failed integrity check: " + expected.path);
            }
        }
        const auto d3d12Core = actual.find(
            Lower("bin/D3D12/D3D12Core.dll"));
        if (d3d12Core == actual.end() ||
            d3d12Core->second.size != RequiredD3D12CoreSize ||
            d3d12Core->second.sha256 != RequiredD3D12CoreSha256)
        {
            throw std::runtime_error(
                "D3D12Core.dll differs from Agility SDK 1.619.5");
        }
        if (actualShaders != expectedShaders)
        {
            throw std::runtime_error(
                "package shader inventory is not the exact runtime contract");
        }
        if (actualProtectedRuntime != expectedProtectedRuntime)
        {
            throw std::runtime_error(
                "package media inventory is not the exact protected runtime contract");
        }
        const FileRecord& engine = actual.at("bin/uvsr-engine.exe");
        if (engine.sha256 != manifest.executableSha256)
            throw std::runtime_error("executable SHA-256 does not match manifest");
        if (verifyMetadata)
            ValidateEngineMetadata(root / "bin" / "uvsr-engine.exe", manifest);
        ValidateSettingsContract(
            root / "bin" / "settings" / "canonical-settings.json", manifest);
    }

    void WriteText(const fs::path& path, std::string_view text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
            throw std::runtime_error("cannot write " + path.string());
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream)
            throw std::runtime_error("cannot finish " + path.string());
    }

    [[nodiscard]] std::string ManifestText(
        const fs::path& root,
        const ShaderInventory& shaders,
        const ProtectedRuntimeInventory& protectedRuntime,
        std::string executableHash = {},
        const std::vector<std::string>& additionalPaths = {},
        std::string_view sourceCommit = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        std::int64_t releaseSequence = 16)
    {
        std::vector<std::string> paths(BasePackagePaths.begin(), BasePackagePaths.end());
        paths.insert(paths.end(), shaders.begin(), shaders.end());
        paths.insert(paths.end(), protectedRuntime.begin(),
            protectedRuntime.end());
        paths.insert(paths.end(), additionalPaths.begin(),
            additionalPaths.end());
        std::sort(paths.begin(), paths.end());
        if (std::adjacent_find(paths.begin(), paths.end()) != paths.end())
            throw std::runtime_error("manifest path is duplicated");
        if (executableHash.empty())
            executableHash = Sha256(root / "bin/uvsr-engine.exe");
        std::string files;
        for (std::size_t index = 0u; index < paths.size(); ++index)
        {
            const fs::path absolute = root / fs::path(paths[index]);
            if (index != 0u)
                files += ',';
            files += "{\"relativePath\":\"" + uvsr::json::Escape(paths[index]) +
                "\",\"size\":" + std::to_string(fs::file_size(absolute)) +
                ",\"sha256\":\"" + Sha256(absolute) + "\"}";
        }
        const std::string settings(uvsr::GetSettingsNumberHashText());
        const std::string engineVersion = uvsr::FormatEngineVersion(uvsr::CurrentEngineVersion);
        return "{\"schemaVersion\":1,\"productId\":\"" +
            std::string(ProductId) +
            "\",\"production\":true,\"releaseSequence\":" +
            std::to_string(releaseSequence) + "," +
            "\"configuration\":\"Release\"," +
            "\"sourceCommit\":\"" +
            uvsr::json::Escape(sourceCommit) + "\",\"settingsHash\":\"" + settings +
            "\",\"engineVersion\":\"" + engineVersion +
            "\",\"executableSha256\":\"" + executableHash +
            "\",\"files\":[" + files + "]}";
    }

    void WritePackageManifest(const fs::path& requestedRoot,
        const ShaderInventory& shaders, const ProtectedRuntimeInventory& protectedRuntime,
        std::string_view commit, std::int64_t sequence)
    {
        if (!IsLowerHex(commit, 40u) || sequence < 1 || sequence > MaximumReleaseSequence)
            throw std::runtime_error("package source or release sequence is invalid");
        RejectReparsePoint(requestedRoot);
        const fs::path root = fs::canonical(requestedRoot);
        Manifest identity{sequence, "Release", std::string(commit),
            std::string(uvsr::GetSettingsNumberHashText()), uvsr::FormatEngineVersion(uvsr::CurrentEngineVersion),
            Sha256(root / "bin/uvsr-engine.exe"), {}};
        ValidateEngineMetadata(root / "bin/uvsr-engine.exe", identity);

        std::vector<std::string> additionalPaths;
        for (const auto& entry : fs::recursive_directory_iterator(root))
        {
            RejectReparsePoint(entry.path());
            const std::string relative = fs::relative(entry.path(), root).generic_string();
            if (entry.is_directory())
            {
                if (!IsAllowedDirectory(relative))
                    throw std::runtime_error("unexpected package directory " + relative);
                continue;
            }
            if (relative == "package-manifest.json")
                continue;
            if (!entry.is_regular_file() || !IsAllowedFile(relative))
                throw std::runtime_error("unexpected package file " + relative);
            if (std::find(BasePackagePaths.begin(), BasePackagePaths.end(), relative) ==
                    BasePackagePaths.end() && !shaders.count(relative) &&
                    !protectedRuntime.count(relative))
                additionalPaths.push_back(relative);
        }
        WriteText(root / "bin/settings/canonical-settings.json", uvsr::BuildSettingsContractJson());
        const std::string manifest = ManifestText(root, shaders, protectedRuntime,
            identity.executableSha256, additionalPaths, commit, sequence);
        (void)ParseManifest(manifest);
        WriteText(root / "package-manifest.json", manifest);
        ValidatePackage(root, true, shaders, protectedRuntime);
    }

    void RequireFailure(const std::function<void()>& operation)
    {
        try
        {
            operation();
        }
        catch (const std::exception&)
        {
            return;
        }
        throw std::runtime_error("invalid package fixture was accepted");
    }

    void SelfTest(
        const ShaderInventory& shaders,
        const ProtectedRuntimeInventory& protectedRuntime,
        const fs::path& d3d12Core,
        const fs::path& shaderInventoryPath,
        const fs::path& assetMapPath)
    {
        const JsonValue equivalent = ParseJson("{\"a\":0,\"b\":[true,\"a\"]}");
        if (!EqualContractValue(equivalent,
                ParseJson("{ \"b\" : [ true, \"\\u0061\" ], \"a\" : -0 }")))
            throw std::runtime_error("equivalent JSON object encodings differ");
        for (const std::string_view different : {
            "{\"a\":0}", "{\"a\":0,\"b\":[\"a\",true]}",
            "{\"a\":false,\"b\":[true,\"a\"]}", "{\"a\":0,\"b\":[true,\"b\"]}" })
            if (EqualContractValue(equivalent, ParseJson(different)))
                throw std::runtime_error("different contract values compared equal");

        const fs::path parent = fs::weakly_canonical(fs::temp_directory_path());
        const fs::path root = parent /
            ("uvsr-renderer-package-contract-" +
                std::to_string(GetCurrentProcessId()));
        if (root.parent_path() != parent ||
            !StartsWith(root.filename().string(), "uvsr-renderer-package-contract-"))
        {
            throw std::runtime_error("unsafe self-test path");
        }
        fs::remove_all(root);
        try
        {
            std::string invalidShaders = ReadFile(
                shaderInventoryPath, 1024u * 1024u);
            invalidShaders += invalidShaders.substr(
                0u, invalidShaders.find('\n') + 1u);
            const fs::path invalidShaderPath =
                root / "invalid-shader-inventory.def";
            WriteText(invalidShaderPath, invalidShaders);
            RequireFailure([&] {
                (void)LoadShaderInventory(invalidShaderPath);
            });

            std::string invalidAssets = ReadFile(
                assetMapPath, 16u * 1024u * 1024u);
            const std::size_t sourceBegin = invalidAssets.find('|') + 1u;
            const std::size_t sourceEnd = invalidAssets.find('\n', sourceBegin);
            invalidAssets.replace(
                sourceBegin, sourceEnd - sourceBegin, "../outside");
            const fs::path invalidAssetPath = root / "invalid-asset-map.def";
            WriteText(invalidAssetPath, invalidAssets);
            RequireFailure([&] {
                (void)LoadRuntimeAssetMap(invalidAssetPath);
            });
            fs::remove(invalidShaderPath);
            fs::remove(invalidAssetPath);

            WriteText(root / "bin/uvsr-engine.exe", "synthetic engine");
            if (fs::file_size(d3d12Core) != RequiredD3D12CoreSize ||
                Sha256(d3d12Core) != RequiredD3D12CoreSha256)
            {
                throw std::runtime_error(
                    "self-test D3D12Core input is not Agility SDK 1.619.5");
            }
            fs::create_directories(root / "bin/D3D12");
            fs::copy_file(d3d12Core,
                root / "bin/D3D12/D3D12Core.dll",
                fs::copy_options::overwrite_existing);
            for (const std::string& shader : shaders)
                WriteText(root / fs::path(shader), "synthetic shader");
            for (const std::string& protectedPath : protectedRuntime)
            {
                WriteText(root / fs::path(protectedPath),
                    "synthetic protected runtime file");
            }
            const fs::path xeGtaoNotice = root /
                "bin/licenses/Intel-XeGTAO-MIT.txt";
            WriteText(xeGtaoNotice, "synthetic XeGTAO notice");
            const RequiredFileInventory selfTestLicenses = {
                {
                    "bin/licenses/Intel-XeGTAO-MIT.txt",
                    fs::file_size(xeGtaoNotice),
                    Sha256(xeGtaoNotice)
                }
            };
            const auto validateFixture = [&]
            {
                ValidatePackage(root, false, shaders, protectedRuntime,
                    selfTestLicenses);
            };
            WriteText(root / "bin/settings/canonical-settings.json",
                uvsr::BuildSettingsContractJson());
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));
            validateFixture();

            const std::string fixtureManifest = ReadFile(root / "package-manifest.json", 16u * 1024u * 1024u);
            RequireFailure([&] {
                WritePackageManifest(root, shaders, protectedRuntime,
                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 16);
            });
            if (ReadFile(root / "package-manifest.json", 16u * 1024u * 1024u) != fixtureManifest)
                throw std::runtime_error("manifest generation changed an unverified package");

            WriteText(root / "bin/D3D12/D3D12Core.dll", "tampered agility");
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));
            RequireFailure(validateFixture);
            fs::copy_file(d3d12Core,
                root / "bin/D3D12/D3D12Core.dll",
                fs::copy_options::overwrite_existing);
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            std::string developerManifest =
                ManifestText(root, shaders, protectedRuntime);
            const std::size_t production =
                developerManifest.find("\"production\":true");
            if (production == std::string::npos)
                throw std::runtime_error("self-test manifest is invalid");
            developerManifest.replace(
                production, sizeof("\"production\":true") - 1u,
                "\"production\":false");
            WriteText(root / "package-manifest.json", developerManifest);
            RequireFailure(validateFixture);
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            std::string debugManifest =
                ManifestText(root, shaders, protectedRuntime);
            const std::size_t releaseConfiguration =
                debugManifest.find("\"configuration\":\"Release\"");
            if (releaseConfiguration == std::string::npos)
                throw std::runtime_error("self-test manifest is invalid");
            debugManifest.replace(
                releaseConfiguration,
                sizeof("\"configuration\":\"Release\"") - 1u,
                "\"configuration\":\"Debug\"");
            WriteText(root / "package-manifest.json", debugManifest);
            RequireFailure(validateFixture);
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            std::string invalidSettings = uvsr::BuildSettingsContractJson();
            const std::size_t membership =
                invalidSettings.find("\"snapshotMember\":true");
            if (membership == std::string::npos)
                throw std::runtime_error("self-test settings fixture is invalid");
            invalidSettings.replace(
                membership, sizeof("\"snapshotMember\":true") - 1u,
                "\"snapshotMember\":false");
            WriteText(root / "bin/settings/canonical-settings.json",
                invalidSettings);
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));
            RequireFailure(validateFixture);
            WriteText(root / "bin/settings/canonical-settings.json",
                uvsr::BuildSettingsContractJson());
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            const fs::path protectedMedia = root /
                "media/uvsr/noise/spatial-blue-64x64x1-r8.bin";
            WriteText(protectedMedia, "tampered media");
            RequireFailure(validateFixture);
            WriteText(protectedMedia, "synthetic protected runtime file");
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            const std::array<std::string_view, 5u> protectedClasses = {
                "bin/licenses/",
                "media/environments/",
                "media/glTF-Sample-Assets/Models/bistro_interior_retextured/",
                "media/glTF-Sample-Assets/Models/san_miguel_retextured/",
                "media/uvsr/noise/"
            };
            for (const std::string_view prefix : protectedClasses)
            {
                const auto member = std::find_if(protectedRuntime.begin(),
                    protectedRuntime.end(), [prefix](const std::string& path)
                    {
                        return StartsWith(path, prefix);
                    });
                if (member == protectedRuntime.end())
                    throw std::runtime_error(
                        "self-test protected class is absent");
                const fs::path missing = root / fs::path(*member);
                fs::remove(missing);
                RequireFailure(validateFixture);
                WriteText(missing, "synthetic protected runtime file");
                WriteText(root / "package-manifest.json",
                    ManifestText(root, shaders, protectedRuntime));
            }

            const fs::path unlistedMedia =
                root / "media/environments/unlisted.hdr";
            WriteText(unlistedMedia, "unlisted but allowlisted media");
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime, {},
                    { "media/environments/unlisted.hdr" }));
            RequireFailure(validateFixture);
            fs::remove(unlistedMedia);
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            WriteText(root / "media/uvsr/noise/README.md", "forbidden doc");
            RequireFailure(validateFixture);
            fs::remove(root / "media/uvsr/noise/README.md");
            for (const std::string_view stale :
                { "debug.pdb", "stale.txt", "catalog.json",
                  "compiler.dll", "probe.exe" })
            {
                const fs::path stalePath = root / "bin/shaders" / stale;
                WriteText(stalePath, "forbidden");
                RequireFailure(validateFixture);
                fs::remove(stalePath);
            }
            const fs::path missingShader = root / fs::path(*shaders.begin());
            fs::remove(missingShader);
            RequireFailure(validateFixture);
            WriteText(missingShader, "synthetic shader");
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));
            const fs::path unlistedShader =
                root / "bin/shaders/unlisted-runtime.bin";
            WriteText(unlistedShader, "forbidden");
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime, {},
                    { "bin/shaders/unlisted-runtime.bin" }));
            RequireFailure(validateFixture);
            fs::remove(unlistedShader);
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            fs::remove(xeGtaoNotice);
            RequireFailure(validateFixture);
            WriteText(xeGtaoNotice, "synthetic XeGTAO notice");
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            WriteText(xeGtaoNotice, "tampered XeGTAO notice");
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));
            RequireFailure(validateFixture);
            WriteText(xeGtaoNotice, "synthetic XeGTAO notice");
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime,
                    std::string(64u, '0')));
            RequireFailure(validateFixture);
        }
        catch (...)
        {
            fs::remove_all(root);
            throw;
        }
        fs::remove_all(root);
    }
}

int main(int argumentCount, char** arguments)
{
    try
    {
        if (argumentCount == 8 &&
            std::string_view(arguments[1]) == "--self-test" &&
            std::string_view(arguments[2]) == "--shader-inventory" &&
            std::string_view(arguments[4]) == "--asset-map" &&
            std::string_view(arguments[6]) == "--d3d12-core")
        {
            SelfTest(LoadShaderInventory(arguments[3]),
                LoadRuntimeAssetMap(arguments[5]), arguments[7],
                arguments[3], arguments[5]);
            std::cout << "renderer package contract self-test passed\n";
            return EXIT_SUCCESS;
        }
        if (argumentCount == 7 &&
            std::string_view(arguments[1]) == "--check" &&
            std::string_view(arguments[3]) == "--shader-inventory" &&
            std::string_view(arguments[5]) == "--asset-map")
        {
            ValidatePackage(arguments[2], true,
                LoadShaderInventory(arguments[4]),
                LoadRuntimeAssetMap(arguments[6]));
            std::cout << "renderer package contract passed\n";
            return EXIT_SUCCESS;
        }
        if (argumentCount == 11 &&
            std::string_view(arguments[1]) == "--write" &&
            std::string_view(arguments[3]) == "--shader-inventory" &&
            std::string_view(arguments[5]) == "--asset-map" &&
            std::string_view(arguments[7]) == "--source-commit" &&
            std::string_view(arguments[9]) == "--release-sequence")
        {
            WritePackageManifest(arguments[2], LoadShaderInventory(arguments[4]),
                LoadRuntimeAssetMap(arguments[6]), arguments[8],
                Integer(ParseJson(arguments[10]), "release sequence"));
            std::cout << "production renderer manifest written and verified\n";
            return EXIT_SUCCESS;
        }
        std::cerr << "usage: uvsr_renderer_package_contract_validator "
            "--check <package-root> --shader-inventory <path> "
            "--asset-map <path> | --self-test --shader-inventory "
            "<path> --asset-map <path> --d3d12-core <path> | --write <package-root> "
            "--shader-inventory <path> --asset-map <path> "
            "--source-commit <commit> --release-sequence <integer>\n";
        return EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        std::cerr << "renderer package contract failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
