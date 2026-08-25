#include "strict_json_contract.h"

#include "engine_identity.h"
#include "settings_snapshot.h"
#include "settings_snapshot_schema.h"
#include "ui_settings_command_catalog.h"

#include <Windows.h>
#include <bcrypt.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
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

    [[nodiscard]] const RequiredFileInventory&
        ProductionRequiredLicenses()
    {
        static const RequiredFileInventory Licenses = {
            {
                "bin/licenses/Intel-XeGTAO-MIT.txt",
                1081u,
                "1f3bfd6b628535f0f0e779e70c260cfdc1bb45bc8644d7108e65f83e24a05cba"
            },
            {
                "bin/licenses/Andrew-Helmer-Stochastic-Generation-MIT.txt",
                1053u,
                "50a4be869e51722a4ca90819535a78df8f82d68facbad52a2da6ab4dc284ad55"
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

    [[nodiscard]] bool IsEngineVersion(std::string_view value)
    {
        unsigned parts = 0u;
        std::size_t position = 0u;
        while (position < value.size())
        {
            const std::size_t begin = position;
            while (position < value.size() && value[position] >= '0' &&
                value[position] <= '9')
            {
                ++position;
            }
            if (begin == position ||
                (value[begin] == '0' && position - begin != 1u))
            {
                return false;
            }
            std::int64_t part = 0;
            const auto parsed = std::from_chars(
                value.data() + begin, value.data() + position, part);
            if (parsed.ec != std::errc{} || part > 65535)
                return false;
            ++parts;
            if (position == value.size())
                break;
            if (value[position++] != '.')
                return false;
        }
        return parts == 4u;
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
        if (StartsWith(path, "media/fonts/NotoSans/"))
            return EndsWith(path, ".ttf");
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
        if (stream.bad() || result.empty())
            throw std::runtime_error("runtime shader inventory is incomplete");
        return result;
    }

    [[nodiscard]] ProtectedRuntimeInventory LoadProtectedRuntimeInventory(
        const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot read runtime media inventory");
        ProtectedRuntimeInventory result;
        std::string previous;
        std::string line;
        std::size_t notices = 0u;
        std::size_t environments = 0u;
        std::size_t fonts = 0u;
        std::size_t bistro = 0u;
        std::size_t sanMiguel = 0u;
        std::size_t noise = 0u;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                throw std::runtime_error(
                    "runtime media inventory must use LF line endings");
            if (line.empty() || !IsAllowedFile(line) ||
                (!StartsWith(line, "media/") &&
                    !StartsWith(line, "bin/licenses/")) ||
                (!previous.empty() && line <= previous) ||
                !result.insert(line).second)
            {
                throw std::runtime_error(
                    "runtime media inventory is not canonical");
            }
            notices += StartsWith(line, "bin/licenses/") ? 1u : 0u;
            environments += StartsWith(line, "media/environments/") ? 1u : 0u;
            fonts += StartsWith(line, "media/fonts/NotoSans/") ? 1u : 0u;
            bistro += StartsWith(line,
                "media/glTF-Sample-Assets/Models/bistro_interior_retextured/")
                ? 1u : 0u;
            sanMiguel += StartsWith(line,
                "media/glTF-Sample-Assets/Models/san_miguel_retextured/")
                ? 1u : 0u;
            noise += StartsWith(line, "media/uvsr/noise/") ? 1u : 0u;
            previous = line;
        }
        if (stream.bad() || result.size() != 310u || notices != 5u ||
            environments != 6u || fonts != 3u || bistro != 7u ||
            sanMiguel != 276u || noise != 13u)
        {
            throw std::runtime_error("runtime media inventory is incomplete");
        }
        return result;
    }

    [[nodiscard]] const char* KindName(
        uvsr::UiSettingsCommandKind kind) noexcept
    {
        using Kind = uvsr::UiSettingsCommandKind;
        switch (kind)
        {
        case Kind::Boolean: return "Boolean";
        case Kind::Integer: return "Integer";
        case Kind::Float: return "Float";
        case Kind::Float3: return "Float3";
        case Kind::Enum: return "Enum";
        case Kind::DynamicSelection: return "DynamicSelection";
        case Kind::Action: return "Action";
        case Kind::Float4: return "Float4";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* PersistenceName(
        uvsr::UiSettingsPersistence persistence) noexcept
    {
        using Persistence = uvsr::UiSettingsPersistence;
        switch (persistence)
        {
        case Persistence::SnapshotCatalog: return "SnapshotCatalog";
        case Persistence::SessionOnly: return "SessionOnly";
        case Persistence::None: return "None";
        }
        return "Unknown";
    }

    [[nodiscard]] std::vector<const uvsr::UiSettingsCommandDefinition*>
    OrderedSettingsValues()
    {
        std::vector<const uvsr::UiSettingsCommandDefinition*> values;
        for (const auto& definition : uvsr::UiSettingsCommandCatalog)
        {
            if (definition.kind != uvsr::UiSettingsCommandKind::Action)
                values.push_back(&definition);
        }
        std::sort(values.begin(), values.end(),
            [](const auto* left, const auto* right)
            {
                return left->name < right->name;
            });
        return values;
    }

    void ValidateSettingsContract(
        const fs::path& path,
        const Manifest& manifest)
    {
        const JsonValue root = ParseJson(ReadFile(path, 1024u * 1024u));
        RequireExactObject(root,
            { "schemaVersion", "settingsHash", "engineVersion",
              "serializationPolicy", "entries" },
            "canonical settings contract");
        if (Integer(Member(root, "schemaVersion"), "settings schema") !=
                uvsr::SettingsSnapshotVersion ||
            String(Member(root, "settingsHash"), "settings hash") !=
                manifest.settingsHash ||
            String(Member(root, "engineVersion"), "engine version") !=
                manifest.engineVersion ||
            String(Member(root, "serializationPolicy"),
                "serialization policy") !=
                uvsr::SettingsSnapshotSerializationPolicy)
        {
            throw std::runtime_error(
                "canonical settings identity does not match manifest");
        }
        const JsonValue& entries = Member(root, "entries");
        const auto expected = OrderedSettingsValues();
        if (entries.kind != JsonValue::Kind::Array ||
            entries.array.size() != expected.size())
        {
            throw std::runtime_error(
                "canonical settings entry count is not authoritative");
        }
        for (std::size_t index = 0u; index < expected.size(); ++index)
        {
            const JsonValue& entry = entries.array[index];
            const auto& definition = *expected[index];
            RequireExactObject(entry,
                { "name", "kind", "persistence", "snapshotMember",
                  "defaultValue", "domain" },
                "canonical settings entry");
            if (String(Member(entry, "name"), "settings name") !=
                    definition.name ||
                String(Member(entry, "kind"), "settings kind") !=
                    KindName(definition.kind) ||
                String(Member(entry, "persistence"),
                    "settings persistence") !=
                    PersistenceName(definition.persistence) ||
                Boolean(Member(entry, "snapshotMember"),
                    "snapshot membership") !=
                    uvsr::IsSettingsSnapshotValue(definition) ||
                String(Member(entry, "defaultValue"),
                    "settings default") != definition.defaultValue ||
                String(Member(entry, "domain"), "settings domain") !=
                    definition.domain)
            {
                throw std::runtime_error(
                    "canonical settings entry differs from compiled catalog");
            }
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
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD objectSize = 0u;
        DWORD resultSize = 0u;
        std::vector<unsigned char> object;
        std::array<unsigned char, 32> digest{};
        const auto cleanup = [&]
        {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0u);
        };
        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0u);
        if (status >= 0)
        {
            status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                &resultSize, 0u);
        }
        if (status >= 0)
        {
            object.resize(objectSize);
            status = BCryptCreateHash(algorithm, &hash, object.data(),
                static_cast<ULONG>(object.size()), nullptr, 0u, 0u);
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            cleanup();
            throw std::runtime_error("cannot hash " + path.string());
        }
        std::vector<char> buffer(1024u * 1024u);
        while (status >= 0 && stream)
        {
            stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = stream.gcount();
            if (count > 0)
            {
                status = BCryptHashData(hash,
                    reinterpret_cast<PUCHAR>(buffer.data()),
                    static_cast<ULONG>(count), 0u);
            }
        }
        if (stream.bad())
            status = -1;
        if (status >= 0)
        {
            status = BCryptFinishHash(hash, digest.data(),
                static_cast<ULONG>(digest.size()), 0u);
        }
        cleanup();
        if (status < 0)
            throw std::runtime_error("Windows SHA-256 operation failed");
        constexpr char Hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(64u);
        for (const unsigned char byte : digest)
        {
            result.push_back(Hex[byte >> 4u]);
            result.push_back(Hex[byte & 0x0fu]);
        }
        return result;
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
            !IsEngineVersion(result.engineVersion) ||
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

    template<typename Value>
    [[nodiscard]] Value ReadPeValue(
        const std::vector<unsigned char>& image,
        std::size_t offset,
        std::string_view description)
    {
        if (offset > image.size() || sizeof(Value) > image.size() - offset)
        {
            throw std::runtime_error(
                "engine PE is truncated at " + std::string(description));
        }
        Value result{};
        std::memcpy(&result, image.data() + offset, sizeof(result));
        return result;
    }

    struct PeImage
    {
        std::vector<unsigned char> bytes;
        IMAGE_FILE_HEADER fileHeader{};
        IMAGE_OPTIONAL_HEADER64 optionalHeader{};
        std::vector<IMAGE_SECTION_HEADER> sections;

        [[nodiscard]] std::size_t FileOffset(
            DWORD rva,
            std::size_t size,
            std::string_view description) const
        {
            if (rva < optionalHeader.SizeOfHeaders)
            {
                const std::size_t offset = rva;
                if (offset <= bytes.size() && size <= bytes.size() - offset)
                    return offset;
            }
            for (const IMAGE_SECTION_HEADER& section : sections)
            {
                const std::uint64_t begin = section.VirtualAddress;
                const std::uint64_t span = std::max<std::uint64_t>(
                    section.Misc.VirtualSize, section.SizeOfRawData);
                const std::uint64_t address = rva;
                if (address < begin || address - begin >= span)
                    continue;
                const std::uint64_t delta = address - begin;
                if (delta > section.SizeOfRawData ||
                    size > section.SizeOfRawData - delta)
                {
                    break;
                }
                const std::uint64_t offset =
                    std::uint64_t(section.PointerToRawData) + delta;
                if (offset <= bytes.size() && size <= bytes.size() - offset)
                    return static_cast<std::size_t>(offset);
                break;
            }
            throw std::runtime_error(
                "engine PE has an invalid " + std::string(description));
        }

        [[nodiscard]] std::string RvaString(
            DWORD rva,
            std::string_view description) const
        {
            const std::size_t offset = FileOffset(rva, 1u, description);
            const std::size_t limit =
                (std::min)(bytes.size(), offset + 260u);
            std::size_t end = offset;
            while (end < limit && bytes[end] != 0u)
                ++end;
            if (end == offset || end == limit)
            {
                throw std::runtime_error(
                    "engine PE has an invalid " + std::string(description));
            }
            return std::string(
                reinterpret_cast<const char*>(bytes.data() + offset),
                end - offset);
        }
    };

    [[nodiscard]] PeImage ReadPeImage(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
            throw std::runtime_error("cannot inspect engine PE");
        const std::streamoff length = stream.tellg();
        if (length < static_cast<std::streamoff>(sizeof(IMAGE_DOS_HEADER)) ||
            length > static_cast<std::streamoff>(1024u * 1024u * 1024u))
        {
            throw std::runtime_error("engine PE size is outside its limit");
        }
        PeImage result;
        result.bytes.resize(static_cast<std::size_t>(length));
        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char*>(result.bytes.data()), length);
        if (!stream)
            throw std::runtime_error("cannot read engine PE");

        const IMAGE_DOS_HEADER dos = ReadPeValue<IMAGE_DOS_HEADER>(
            result.bytes, 0u, "DOS header");
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
            throw std::runtime_error("engine is not a canonical PE image");
        const std::size_t ntOffset = static_cast<std::size_t>(dos.e_lfanew);
        const DWORD signature = ReadPeValue<DWORD>(
            result.bytes, ntOffset, "NT signature");
        if (signature != IMAGE_NT_SIGNATURE)
            throw std::runtime_error("engine has no PE signature");
        const std::size_t fileOffset = ntOffset + sizeof(DWORD);
        result.fileHeader = ReadPeValue<IMAGE_FILE_HEADER>(
            result.bytes, fileOffset, "COFF header");
        if (result.fileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            result.fileHeader.NumberOfSections == 0u ||
            result.fileHeader.NumberOfSections > 96u ||
            result.fileHeader.PointerToSymbolTable != 0u ||
            result.fileHeader.NumberOfSymbols != 0u ||
            result.fileHeader.SizeOfOptionalHeader !=
                sizeof(IMAGE_OPTIONAL_HEADER64))
        {
            throw std::runtime_error(
                "engine PE architecture or symbol contract is invalid");
        }
        const std::size_t optionalOffset =
            fileOffset + sizeof(IMAGE_FILE_HEADER);
        result.optionalHeader = ReadPeValue<IMAGE_OPTIONAL_HEADER64>(
            result.bytes, optionalOffset, "optional header");
        if (result.optionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            result.optionalHeader.Subsystem != IMAGE_SUBSYSTEM_WINDOWS_GUI ||
            result.optionalHeader.NumberOfRvaAndSizes <=
                IMAGE_DIRECTORY_ENTRY_IMPORT)
        {
            throw std::runtime_error("engine PE header is not canonical");
        }
        const std::size_t sectionsOffset =
            optionalOffset + result.fileHeader.SizeOfOptionalHeader;
        for (WORD index = 0u; index < result.fileHeader.NumberOfSections;
            ++index)
        {
            const IMAGE_SECTION_HEADER section =
                ReadPeValue<IMAGE_SECTION_HEADER>(result.bytes,
                    sectionsOffset +
                        std::size_t(index) * sizeof(IMAGE_SECTION_HEADER),
                    "section table");
            std::array<char, IMAGE_SIZEOF_SHORT_NAME + 1u> name{};
            std::memcpy(name.data(), section.Name, IMAGE_SIZEOF_SHORT_NAME);
            if (StartsWith(Lower(name.data()), ".debug"))
                throw std::runtime_error("engine contains a debug section");
            result.sections.push_back(section);
        }
        return result;
    }

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
                ReadPeValue<IMAGE_IMPORT_DESCRIPTOR>(image.bytes,
                    offset + index * sizeof(entry), "import descriptor");
            if (entry.Name == 0u && entry.FirstThunk == 0u &&
                entry.OriginalFirstThunk == 0u)
            {
                terminated = true;
                break;
            }
            if (entry.Name == 0u)
                throw std::runtime_error("engine PE import name is missing");
            const std::string name = image.RvaString(entry.Name, "import name");
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
                ReadPeValue<IMAGE_DEBUG_DIRECTORY>(image.bytes,
                    offset + index * sizeof(entry), "debug entry");
            if (entry.Type == IMAGE_DEBUG_TYPE_CODEVIEW || entry.Type == 17u)
            {
                throw std::runtime_error(
                    "engine contains embedded symbol information");
            }
        }
    }

    void ValidateEnginePeContract(const fs::path& engine)
    {
        const PeImage image = ReadPeImage(engine);
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

        bool hasEngine = false;
        bool hasD3d12Core = false;
        bool hasShader = false;
        bool hasLicense = false;
        bool hasMedia = false;
        bool hasSettings = false;
        ShaderInventory manifestShaders;
        ProtectedRuntimeInventory manifestProtectedRuntime;
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
            hasEngine = hasEngine || expected.path == "bin/uvsr-engine.exe";
            hasD3d12Core = hasD3d12Core ||
                expected.path == "bin/D3D12/D3D12Core.dll";
            if (StartsWith(expected.path, "bin/shaders/"))
            {
                hasShader = true;
                manifestShaders.insert(expected.path);
            }
            hasLicense = hasLicense || StartsWith(expected.path, "bin/licenses/");
            hasSettings = hasSettings ||
                expected.path == "bin/settings/canonical-settings.json";
            hasMedia = hasMedia || StartsWith(expected.path, "media/");
            if (StartsWith(expected.path, "media/") ||
                expectedProtectedRuntime.count(expected.path) != 0u)
            {
                manifestProtectedRuntime.insert(expected.path);
            }
        }
        if (!hasEngine || !hasD3d12Core || !hasShader || !hasLicense ||
            !hasSettings || !hasMedia)
        {
            throw std::runtime_error("package inventory is incomplete");
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
        ShaderInventory actualShaders;
        ProtectedRuntimeInventory actualProtectedRuntime;
        for (const auto& [ignored, file] : actual)
        {
            if (StartsWith(file.path, "bin/shaders/"))
                actualShaders.insert(file.path);
            if (StartsWith(file.path, "media/") ||
                expectedProtectedRuntime.count(file.path) != 0u)
            {
                actualProtectedRuntime.insert(file.path);
            }
        }
        if (manifestShaders != expectedShaders ||
            actualShaders != expectedShaders)
        {
            throw std::runtime_error(
                "package shader inventory is not the exact runtime contract");
        }
        if (manifestProtectedRuntime != expectedProtectedRuntime ||
            actualProtectedRuntime != expectedProtectedRuntime)
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
            throw std::runtime_error("cannot write self-test fixture");
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream)
            throw std::runtime_error("cannot finish self-test fixture");
    }

    [[nodiscard]] std::string EscapeJson(std::string_view value)
    {
        constexpr char Hex[] = "0123456789abcdef";
        std::string result;
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20u)
                {
                    result += "\\u00";
                    result.push_back(Hex[character >> 4u]);
                    result.push_back(Hex[character & 0x0fu]);
                }
                else
                    result.push_back(static_cast<char>(character));
            }
        }
        return result;
    }

    [[nodiscard]] std::string SettingsContractText()
    {
        constexpr std::string_view Hash =
            "9c50b0f1515e89d856c8ebb627b86984";
        constexpr std::string_view EngineVersion =
            "40016.45297.20830.35288";
        std::string result =
            "{\"schemaVersion\":" +
            std::to_string(uvsr::SettingsSnapshotVersion) +
            ",\"settingsHash\":\"" + std::string(Hash) +
            "\",\"engineVersion\":\"" + std::string(EngineVersion) +
            "\",\"serializationPolicy\":\"" +
            EscapeJson(uvsr::SettingsSnapshotSerializationPolicy) +
            "\",\"entries\":[";
        const auto values = OrderedSettingsValues();
        for (std::size_t index = 0u; index < values.size(); ++index)
        {
            const auto& definition = *values[index];
            if (index != 0u)
                result.push_back(',');
            result += "{\"name\":\"" + EscapeJson(definition.name) +
                "\",\"kind\":\"" + KindName(definition.kind) +
                "\",\"persistence\":\"" +
                PersistenceName(definition.persistence) +
                "\",\"snapshotMember\":" +
                (uvsr::IsSettingsSnapshotValue(definition) ? "true" : "false") +
                ",\"defaultValue\":\"" +
                EscapeJson(definition.defaultValue) +
                "\",\"domain\":\"" + EscapeJson(definition.domain) +
                "\"}";
        }
        result += "]}\n";
        return result;
    }

    [[nodiscard]] std::string ManifestText(
        const fs::path& root,
        const ShaderInventory& shaders,
        const ProtectedRuntimeInventory& protectedRuntime,
        std::string executableHash = {},
        const std::vector<std::string>& additionalPaths = {})
    {
        std::vector<std::string> paths = {
            "bin/D3D12/D3D12Core.dll",
            "bin/licenses/Andrew-Helmer-Stochastic-Generation-MIT.txt",
            "bin/licenses/Intel-XeGTAO-MIT.txt",
            "bin/settings/canonical-settings.json",
            "bin/uvsr-engine.exe"
        };
        paths.insert(paths.end(), shaders.begin(), shaders.end());
        paths.insert(paths.end(), protectedRuntime.begin(),
            protectedRuntime.end());
        paths.insert(paths.end(), additionalPaths.begin(),
            additionalPaths.end());
        std::sort(paths.begin(), paths.end());
        if (std::adjacent_find(paths.begin(), paths.end()) != paths.end())
            throw std::runtime_error("self-test manifest path is duplicated");
        if (executableHash.empty())
            executableHash = Sha256(root / "bin/uvsr-engine.exe");
        std::string files;
        for (std::size_t index = 0u; index < paths.size(); ++index)
        {
            const fs::path absolute = root / fs::path(paths[index]);
            if (index != 0u)
                files += ',';
            files += "{\"relativePath\":\"" + paths[index] +
                "\",\"size\":" + std::to_string(fs::file_size(absolute)) +
                ",\"sha256\":\"" + Sha256(absolute) + "\"}";
        }
        const std::string settings = "9c50b0f1515e89d856c8ebb627b86984";
        const std::string engineVersion = "40016.45297.20830.35288";
        return "{\"schemaVersion\":1,\"productId\":\"" +
            std::string(ProductId) +
            "\",\"production\":true,\"releaseSequence\":16," +
            "\"configuration\":\"Release\"," +
            "\"sourceCommit\":\"" +
            std::string(40u, 'a') + "\",\"settingsHash\":\"" + settings +
            "\",\"engineVersion\":\"" + engineVersion +
            "\",\"executableSha256\":\"" + executableHash +
            "\",\"files\":[" + files + "]}";
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
        const fs::path& d3d12Core)
    {
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
            const fs::path helmerNotice = root /
                "bin/licenses/Andrew-Helmer-Stochastic-Generation-MIT.txt";
            const fs::path xeGtaoNotice = root /
                "bin/licenses/Intel-XeGTAO-MIT.txt";
            WriteText(helmerNotice, "synthetic Helmer notice");
            WriteText(xeGtaoNotice, "synthetic XeGTAO notice");
            const RequiredFileInventory selfTestLicenses = {
                {
                    "bin/licenses/Andrew-Helmer-Stochastic-Generation-MIT.txt",
                    fs::file_size(helmerNotice),
                    Sha256(helmerNotice)
                },
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
                SettingsContractText());
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));
            validateFixture();

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

            std::string invalidSettings = SettingsContractText();
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
                SettingsContractText());
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            const fs::path protectedMedia = root /
                "media/uvsr/noise/spatial-blue-64x64x1-r8.bin";
            WriteText(protectedMedia, "tampered media");
            RequireFailure(validateFixture);
            WriteText(protectedMedia, "synthetic protected runtime file");
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));

            const std::array<std::string_view, 6u> protectedClasses = {
                "bin/licenses/",
                "media/environments/",
                "media/fonts/NotoSans/",
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

            WriteText(helmerNotice, "tampered Helmer notice");
            WriteText(root / "package-manifest.json",
                ManifestText(root, shaders, protectedRuntime));
            RequireFailure(validateFixture);
            WriteText(helmerNotice, "synthetic Helmer notice");
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
            std::string_view(arguments[4]) == "--media-inventory" &&
            std::string_view(arguments[6]) == "--d3d12-core")
        {
            SelfTest(LoadShaderInventory(arguments[3]),
                LoadProtectedRuntimeInventory(arguments[5]), arguments[7]);
            std::cout << "renderer package contract self-test passed\n";
            return EXIT_SUCCESS;
        }
        if (argumentCount == 7 &&
            std::string_view(arguments[1]) == "--check" &&
            std::string_view(arguments[3]) == "--shader-inventory" &&
            std::string_view(arguments[5]) == "--media-inventory")
        {
            ValidatePackage(arguments[2], true,
                LoadShaderInventory(arguments[4]),
                LoadProtectedRuntimeInventory(arguments[6]));
            std::cout << "renderer package contract passed\n";
            return EXIT_SUCCESS;
        }
        std::cerr << "usage: uvsr_renderer_package_contract_validator "
            "--check <package-root> --shader-inventory <path> "
            "--media-inventory <path> | --self-test --shader-inventory "
            "<path> --media-inventory <path> --d3d12-core <path>\n";
        return EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        std::cerr << "renderer package contract failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
