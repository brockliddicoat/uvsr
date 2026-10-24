#include "internal.h"
#include "resource_ids.h"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <unordered_set>

namespace fs = std::filesystem;

namespace uvsr::launcher
{
    namespace
    {
        constexpr std::string_view ManifestName = "package-manifest.json";
        constexpr std::string_view EnginePath = "bin/uvsr-engine.exe";
        constexpr std::string_view D3d12CorePath = "bin/D3D12/D3D12Core.dll";
        constexpr std::string_view SettingsPath =
            "bin/settings/canonical-settings.json";

        struct CaseInsensitiveLess
        {
            using is_transparent = void;

            bool operator()(std::string_view left, std::string_view right) const
                noexcept
            {
                return std::lexicographical_compare(left.begin(), left.end(),
                    right.begin(), right.end(), [](char a, char b)
                    {
                        return std::tolower(static_cast<unsigned char>(a)) <
                            std::tolower(static_cast<unsigned char>(b));
                    });
            }
        };

        using PathSet = std::set<std::string, std::less<>>;
        using WindowsPathSet = std::set<std::string, CaseInsensitiveLess>;

        struct Inventory
        {
            PathSet shaders;
            PathSet media;
            PathSet notices;
            PathSet protectedPaths;
            PathSet shaderDirectories;
            PathSet mediaDirectories;
        };

        [[nodiscard]] bool ends_with_case_insensitive(
            std::string_view value,
            std::string_view suffix)
        {
            if (value.size() < suffix.size())
                return false;
            const auto tail = value.substr(value.size() - suffix.size());
            return std::equal(tail.begin(), tail.end(), suffix.begin(),
                suffix.end(), [](char left, char right)
                {
                    return std::tolower(static_cast<unsigned char>(left)) ==
                        std::tolower(static_cast<unsigned char>(right));
                });
        }

        [[nodiscard]] bool equals_case_insensitive(
            std::string_view left,
            std::string_view right) noexcept
        {
            const CaseInsensitiveLess less;
            return !less(left, right) && !less(right, left);
        }

        [[nodiscard]] bool begins_with(
            std::string_view value,
            std::string_view prefix) noexcept
        {
            return value.size() >= prefix.size() &&
                value.substr(0, prefix.size()) == prefix;
        }

        [[nodiscard]] std::vector<std::string> strict_lines(
            std::string text,
            std::size_t expected,
            std::string_view description)
        {
            if (text.empty() || text.find('\r') != std::string::npos ||
                text.back() != '\n' ||
                (text.size() >= 2u && text[text.size() - 2u] == '\n'))
            {
                throw Error(std::string(description) + " was invalid.");
            }
            text.pop_back();
            std::vector<std::string> result;
            std::size_t begin = 0;
            for (;;)
            {
                const std::size_t end = text.find('\n', begin);
                result.emplace_back(text.substr(begin, end - begin));
                if (end == std::string::npos)
                    break;
                begin = end + 1u;
            }
            if (result.size() != expected ||
                !std::is_sorted(result.begin(), result.end()) ||
                std::adjacent_find(result.begin(), result.end()) != result.end())
            {
                throw Error(std::string(description) + " was invalid.");
            }
            for (const auto& path : result)
            {
                if (path.empty() || path.find('\\') != std::string::npos ||
                    path.front() == '/' || path.find(':') != std::string::npos)
                {
                    throw Error(std::string(description) + " was invalid.");
                }
                std::size_t partBegin = 0;
                for (;;)
                {
                    const std::size_t slash = path.find('/', partBegin);
                    const auto part = std::string_view(path).substr(
                        partBegin, slash - partBegin);
                    if (part.empty() || part == "." || part == "..")
                        throw Error(std::string(description) + " was invalid.");
                    if (slash == std::string::npos)
                        break;
                    partBegin = slash + 1u;
                }
            }
            return result;
        }

        void add_directories(PathSet& result, std::string path)
        {
            for (;;)
            {
                const std::size_t slash = path.rfind('/');
                if (slash == std::string::npos)
                    return;
                path.resize(slash);
                result.insert(path);
            }
        }

        [[nodiscard]] bool is_environment_path(std::string_view path)
        {
            return begins_with(path, "media/environments/") &&
                path.ends_with(".hdr");
        }

        [[nodiscard]] bool is_noise_path(std::string_view path)
        {
            constexpr std::string_view prefix = "media/uvsr/noise/";
            if (!begins_with(path, prefix))
                return false;
            const auto relative = path.substr(prefix.size());
            return relative.find('/') == std::string_view::npos &&
                (relative == "manifest.json" || relative.ends_with(".bin"));
        }

        [[nodiscard]] bool is_scene_path(
            std::string_view path,
            std::string_view scene)
        {
            const std::string prefix =
                "media/glTF-Sample-Assets/Models/" + std::string(scene) + "/";
            return begins_with(path, prefix) &&
                (path.ends_with(".scene.json") || path.ends_with(".gltf") ||
                    path.ends_with(".glb") || path.ends_with(".bin") ||
                    path.ends_with(".png"));
        }

        [[nodiscard]] bool is_noto_path(std::string_view path)
        {
            constexpr std::string_view prefix = "media/fonts/NotoSans/";
            if (!begins_with(path, prefix))
                return false;
            const auto relative = path.substr(prefix.size());
            return relative.find('/') == std::string_view::npos &&
                relative.ends_with(".ttf");
        }

        [[nodiscard]] Inventory load_inventory()
        {
            Inventory result;
            const auto shaders = strict_lines(
                resource_text(IDR_SHADER_INVENTORY), 47u,
                "The launcher runtime shader inventory resource");
            for (const auto& path : shaders)
            {
                if (!begins_with(path, "bin/shaders/") ||
                    !path.ends_with(".bin"))
                {
                    throw Error(
                        "The launcher runtime shader inventory resource was invalid.");
                }
                result.shaders.insert(path);
                add_directories(result.shaderDirectories, path);
            }

            const auto retained = strict_lines(
                resource_text(IDR_MEDIA_INVENTORY), 310u,
                "The launcher retained media inventory resource");
            for (const auto& path : retained)
            {
                result.protectedPaths.insert(path);
                if (begins_with(path, "bin/licenses/"))
                    result.notices.insert(path);
                else if (begins_with(path, "media/"))
                {
                    result.media.insert(path);
                    add_directories(result.mediaDirectories, path);
                }
                else
                    throw Error(
                        "The launcher retained media inventory resource was invalid.");
            }
            const auto count = [&](auto predicate)
            {
                return static_cast<std::size_t>(std::count_if(
                    result.media.begin(), result.media.end(), predicate));
            };
            if (result.media.size() != 305u || result.notices.size() != 5u ||
                count(is_environment_path) != 6u ||
                count(is_noise_path) != 13u ||
                count([](const std::string& path)
                    { return is_scene_path(path, "bistro_interior_retextured"); }) != 7u ||
                count([](const std::string& path)
                    { return is_scene_path(path, "san_miguel_retextured"); }) != 276u ||
                count(is_noto_path) != 3u)
            {
                throw Error(
                    "The launcher retained media inventory resource was invalid.");
            }
            return result;
        }

        [[nodiscard]] const Inventory& inventory()
        {
            static const Inventory value = load_inventory();
            return value;
        }

        [[nodiscard]] bool is_forbidden_developer_file(std::string_view path)
        {
            if (equals_case_insensitive(
                    path, "bin/D3D12/D3D12SDKLayers.dll"))
                return true;
            const std::size_t slash = path.rfind('/');
            const auto name = path.substr(
                slash == std::string_view::npos ? 0u : slash + 1u);
            if (name.size() >= 4u &&
                std::tolower(static_cast<unsigned char>(name[0])) == '.' &&
                std::tolower(static_cast<unsigned char>(name[1])) == 'g' &&
                std::tolower(static_cast<unsigned char>(name[2])) == 'i' &&
                std::tolower(static_cast<unsigned char>(name[3])) == 't')
            {
                return true;
            }
            constexpr std::array extensions = {
                ".py", ".pyc", ".ps1", ".cmd", ".bat", ".cmake", ".cpp",
                ".cxx", ".cc", ".h", ".hpp", ".hlsl", ".hlsli", ".pdb",
                ".ilk", ".lib", ".exp", ".obj", ".sln", ".vcxproj"
            };
            return std::any_of(extensions.begin(), extensions.end(),
                [&](std::string_view extension)
                {
                    return ends_with_case_insensitive(name, extension);
                });
        }

        [[nodiscard]] bool is_allowed_file(std::string_view path)
        {
            if (is_forbidden_developer_file(path))
                return false;
            const auto& values = inventory();
            return path == EnginePath || path == D3d12CorePath ||
                path == SettingsPath || values.shaders.contains(path) ||
                begins_with(path, "bin/licenses/") ||
                values.media.contains(path);
        }

        [[nodiscard]] bool is_allowed_directory(std::string_view path)
        {
            if (path == "bin" || path == "media" || path == "bin/D3D12" ||
                path == "bin/shaders" || path == "bin/licenses" ||
                path == "bin/settings")
            {
                return true;
            }
            const auto& values = inventory();
            return values.shaderDirectories.contains(path) ||
                begins_with(path, "bin/licenses/") ||
                values.mediaDirectories.contains(path);
        }

        [[nodiscard]] bool reserved_windows_component(
            std::wstring_view component)
        {
            if (component.empty() || component.size() > 255u ||
                component.back() == L'.' || component.back() == L' ')
            {
                return true;
            }
            const std::size_t dot = component.find(L'.');
            const std::wstring_view stem = component.substr(0, dot);
            constexpr std::array names = {
                std::wstring_view(L"CON"), std::wstring_view(L"PRN"),
                std::wstring_view(L"AUX"), std::wstring_view(L"NUL"),
                std::wstring_view(L"CLOCK$")
            };
            if (std::ranges::any_of(names, [&](std::wstring_view name)
                {
                    return CompareStringOrdinal(stem.data(),
                               static_cast<int>(stem.size()), name.data(),
                               static_cast<int>(name.size()), TRUE) == CSTR_EQUAL;
                }))
            {
                return true;
            }
            if (stem.size() != 4u)
                return false;
            const bool numberedDevice =
                CompareStringOrdinal(stem.data(), 3, L"COM", 3, TRUE) ==
                    CSTR_EQUAL ||
                CompareStringOrdinal(stem.data(), 3, L"LPT", 3, TRUE) ==
                    CSTR_EQUAL;
            const wchar_t digit = stem.back();
            return numberedDevice && ((digit >= L'1' && digit <= L'9') ||
                digit == L'\u00b9' || digit == L'\u00b2' || digit == L'\u00b3');
        }

        [[nodiscard]] std::string normalize_relative(std::string_view value)
        {
            if (value.empty() || value.front() == '/' ||
                value.find(':') != std::string_view::npos ||
                value.find('\\') != std::string_view::npos ||
                std::any_of(value.begin(), value.end(), [](unsigned char value)
                {
                    return value < 0x20u || value == 0x7fu || value == '<' ||
                        value == '>' || value == '"' || value == '|' ||
                        value == '?' || value == '*';
                }))
            {
                throw Error("A renderer package relative path was invalid.");
            }
            static_cast<void>(widen(value));
            std::size_t begin = 0;
            for (;;)
            {
                const std::size_t slash = value.find('/', begin);
                const auto part = value.substr(begin, slash - begin);
                if (part.empty() || part == "." || part == ".." ||
                    reserved_windows_component(widen(part)))
                    throw Error("A renderer package relative path was invalid.");
                if (slash == std::string_view::npos)
                    break;
                begin = slash + 1u;
            }
            return std::string(value);
        }

        [[nodiscard]] bool hash_equal(
            std::string_view left,
            std::string_view right) noexcept
        {
            if (!is_lower_hex(left, 64u) || !is_lower_hex(right, 64u))
                return false;
            unsigned difference = 0;
            for (std::size_t index = 0; index < left.size(); ++index)
                difference |= static_cast<unsigned>(left[index] ^ right[index]);
            return difference == 0;
        }

        void validate_manifest_contract(const PackageManifest& manifest)
        {
            if (manifest.schemaVersion != 1 || manifest.productId != ProductId ||
                !manifest.production || manifest.configuration != "Release" ||
                manifest.releaseSequence < 1 ||
                manifest.releaseSequence > MaximumReleaseSequence ||
                !is_lower_hex(manifest.sourceCommit, 40u) ||
                !is_lower_hex(manifest.settingsHash, 32u) ||
                !is_engine_version(manifest.engineVersion) ||
                !is_lower_hex(manifest.executableSha256, 64u) ||
                manifest.files.empty() || manifest.files.size() > 100000u)
            {
                throw Error(
                    "The renderer package manifest did not match the required strict contract.");
            }
        }

        [[nodiscard]] PackageManifest parse_manifest(
            const detail::Json& root)
        {
            detail::require_exact_object(root, {
                "schemaVersion", "productId", "production", "configuration",
                "releaseSequence", "sourceCommit", "settingsHash",
                "engineVersion", "executableSha256", "files" },
                "The renderer package manifest");
            PackageManifest result;
            result.schemaVersion = static_cast<int>(detail::integer_value(
                detail::member(root, "schemaVersion"), "schemaVersion"));
            result.productId = detail::string_value(
                detail::member(root, "productId"), "productId");
            result.production = detail::boolean_value(
                detail::member(root, "production"), "production");
            result.configuration = detail::string_value(
                detail::member(root, "configuration"), "configuration");
            result.releaseSequence = detail::integer_value(
                detail::member(root, "releaseSequence"), "releaseSequence");
            result.sourceCommit = detail::string_value(
                detail::member(root, "sourceCommit"), "sourceCommit");
            result.settingsHash = detail::string_value(
                detail::member(root, "settingsHash"), "settingsHash");
            result.engineVersion = detail::string_value(
                detail::member(root, "engineVersion"), "engineVersion");
            result.executableSha256 = detail::string_value(
                detail::member(root, "executableSha256"), "executableSha256");
            const auto& files = detail::member(root, "files");
            if (files.kind != detail::Json::Kind::Array)
                throw Error("The renderer package file inventory was invalid.");
            result.files.reserve(files.array.size());
            for (const auto& item : files.array)
            {
                detail::require_exact_object(item,
                    { "relativePath", "size", "sha256" },
                    "A renderer package file record");
                result.files.push_back({
                    detail::string_value(detail::member(item, "relativePath"),
                        "relativePath"),
                    detail::unsigned_value(detail::member(item, "size"),
                        "size"),
                    detail::string_value(detail::member(item, "sha256"),
                        "sha256") });
            }
            validate_manifest_contract(result);
            return result;
        }

        void reject_reparse_tree(const fs::path& root)
        {
            reject_reparse_chain(root, "UVSR runtime package");
            std::error_code error;
            fs::recursive_directory_iterator iterator(root,
                fs::directory_options::none, error);
            if (error)
                throw Error("The UVSR runtime package could not be inspected.");
            for (const auto& entry : iterator)
            {
                const DWORD attributes = GetFileAttributesW(entry.path().c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES)
                    detail::throw_last_error("Windows could not inspect a package file");
                if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                {
                    iterator.disable_recursion_pending();
                    throw Error(
                        "The UVSR runtime package contained an unsafe reparse point.");
                }
            }
        }

        [[nodiscard]] std::vector<PackageFile> actual_files(
            const fs::path& packageRoot)
        {
            std::vector<PackageFile> result;
            for (const auto& entry : fs::recursive_directory_iterator(packageRoot))
            {
                if (!entry.is_regular_file())
                {
                    if (entry.is_directory())
                        continue;
                    throw Error(
                        "The renderer package contained an unsupported filesystem entry.");
                }
                std::string relative = entry.path().lexically_relative(packageRoot)
                    .generic_string();
                relative = normalize_relative(relative);
                if (relative == ManifestName)
                    continue;
                if (!is_allowed_file(relative))
                    throw Error("The renderer package contained an unexpected file '" +
                        relative + "'.");
                result.push_back({ relative, entry.file_size(),
                    sha256(entry.path()) });
            }
            std::sort(result.begin(), result.end(),
                [](const PackageFile& left, const PackageFile& right)
                { return left.relativePath < right.relativePath; });
            return result;
        }

        void validate_settings(
            const fs::path& packageRoot,
            const PackageManifest& manifest)
        {
            const auto text = detail::read_text(
                packageRoot / fs::path(std::string(SettingsPath)),
                16ull * 1024ull * 1024ull);
            const auto root = detail::parse_json(text);
            detail::require_exact_object(root, {
                "schemaVersion", "settingsHash", "engineVersion",
                "serializationPolicy", "entries" },
                "The canonical settings contract");
            if (detail::integer_value(detail::member(root, "schemaVersion"),
                    "schemaVersion") != SettingsContractSchemaVersion ||
                detail::string_value(detail::member(root, "settingsHash"),
                    "settingsHash") != manifest.settingsHash ||
                detail::string_value(detail::member(root, "engineVersion"),
                    "engineVersion") != manifest.engineVersion)
            {
                throw Error(
                    "The canonical settings contract identity did not match the package.");
            }
            const auto policy = detail::string_value(
                detail::member(root, "serializationPolicy"),
                "serializationPolicy");
            const auto& entries = detail::member(root, "entries");
            if (policy.empty() || policy.size() > 4096u ||
                entries.kind != detail::Json::Kind::Array ||
                entries.array.size() != 176u)
            {
                throw Error("The canonical settings contract was invalid.");
            }
            std::string previous;
            std::size_t snapshotCount = 0;
            std::size_t sessionCount = 0;
            for (const auto& entry : entries.array)
            {
                detail::require_exact_object(entry, {
                    "name", "kind", "persistence", "snapshotMember",
                    "defaultValue", "domain" }, "A settings contract entry");
                const std::string name = detail::string_value(
                    detail::member(entry, "name"), "name");
                const std::string kind = detail::string_value(
                    detail::member(entry, "kind"), "kind");
                const std::string persistence = detail::string_value(
                    detail::member(entry, "persistence"), "persistence");
                const bool snapshot = detail::boolean_value(
                    detail::member(entry, "snapshotMember"), "snapshotMember");
                const std::string defaultValue = detail::string_value(
                    detail::member(entry, "defaultValue"), "defaultValue");
                const std::string domain = detail::string_value(
                    detail::member(entry, "domain"), "domain");
                const bool knownKind = kind == "Boolean" || kind == "Integer" ||
                    kind == "Float" || kind == "Float3" || kind == "Enum" ||
                    kind == "DynamicSelection" || kind == "Float4";
                if (name.empty() || name.size() > 256u || !knownKind ||
                    (!previous.empty() && previous >= name) ||
                    defaultValue.size() > 4096u || domain.size() > 4096u)
                {
                    throw Error("A canonical settings contract entry was invalid.");
                }
                if (persistence == "SnapshotCatalog" && snapshot)
                    ++snapshotCount;
                else if (persistence == "SessionOnly" && !snapshot)
                    ++sessionCount;
                else
                    throw Error(
                        "Settings persistence and snapshot membership disagreed.");
                previous = name;
            }
            if (snapshotCount != 174u || sessionCount != 2u)
                throw Error("Settings snapshot membership was invalid.");
        }

        struct ZipSource
        {
            HANDLE handle = INVALID_HANDLE_VALUE;
        };

        size_t zip_read(
            void* opaque,
            mz_uint64 offset,
            void* buffer,
            size_t bytes)
        {
            auto& source = *static_cast<ZipSource*>(opaque);
            LARGE_INTEGER position{};
            position.QuadPart = static_cast<LONGLONG>(offset);
            if (!SetFilePointerEx(source.handle, position, nullptr, FILE_BEGIN))
                return 0;
            std::size_t total = 0;
            while (total < bytes)
            {
                const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
                    bytes - total, std::numeric_limits<DWORD>::max()));
                DWORD read = 0;
                if (!ReadFile(source.handle,
                        static_cast<std::byte*>(buffer) + total,
                        requested, &read, nullptr))
                {
                    return 0;
                }
                if (read == 0)
                    break;
                total += read;
            }
            return total;
        }

        class ZipArchive final
        {
        public:
            explicit ZipArchive(const fs::path& path)
            {
                m_Source.handle = CreateFileW(path.c_str(), GENERIC_READ,
                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
                if (m_Source.handle == INVALID_HANDLE_VALUE)
                    detail::throw_last_error("Windows could not open the renderer package");
                LARGE_INTEGER size{};
                if (!GetFileSizeEx(m_Source.handle, &size) || size.QuadPart <= 0)
                {
                    CloseHandle(m_Source.handle);
                    m_Source.handle = INVALID_HANDLE_VALUE;
                    throw Error("The renderer package archive was empty or invalid.");
                }
                m_Zip.m_pRead = zip_read;
                m_Zip.m_pIO_opaque = &m_Source;
                if (!mz_zip_reader_init(&m_Zip,
                        static_cast<mz_uint64>(size.QuadPart), 0))
                {
                    CloseHandle(m_Source.handle);
                    m_Source.handle = INVALID_HANDLE_VALUE;
                    throw Error("The renderer package was not a valid ZIP archive.");
                }
                m_Initialized = true;
            }

            ~ZipArchive()
            {
                if (m_Initialized)
                    mz_zip_reader_end(&m_Zip);
                if (m_Source.handle != INVALID_HANDLE_VALUE)
                    CloseHandle(m_Source.handle);
            }

            ZipArchive(const ZipArchive&) = delete;
            ZipArchive& operator=(const ZipArchive&) = delete;

            [[nodiscard]] mz_zip_archive* get() noexcept { return &m_Zip; }

        private:
            ZipSource m_Source;
            mz_zip_archive m_Zip{};
            bool m_Initialized = false;
        };

        struct ArchiveEntry
        {
            mz_uint index = 0;
            std::string path;
            std::uint64_t size = 0;
            bool directory = false;
        };

        [[nodiscard]] std::vector<ArchiveEntry> inspect_archive(
            ZipArchive& archive,
            std::uint64_t& expanded)
        {
            const mz_uint count = mz_zip_reader_get_num_files(archive.get());
            if (count == 0u || count > 100001u)
                throw Error("The renderer package archive inventory was invalid.");
            std::vector<ArchiveEntry> result;
            result.reserve(count);
            WindowsPathSet names;
            expanded = 0;
            for (mz_uint index = 0; index < count; ++index)
            {
                mz_zip_archive_file_stat stat{};
                if (!mz_zip_reader_file_stat(archive.get(), index, &stat))
                    throw Error("The renderer package archive inventory was invalid.");
                const mz_uint characters = mz_zip_reader_get_filename(
                    archive.get(), index, nullptr, 0);
                if (characters <= 1u || characters > 32768u)
                    throw Error(
                        "The renderer package archive contained an invalid path.");
                std::string path(characters, '\0');
                if (mz_zip_reader_get_filename(archive.get(), index,
                        path.data(), characters) != characters)
                {
                    throw Error(
                        "The renderer package archive contained an invalid path.");
                }
                path.resize(characters - 1u);
                const bool directory = mz_zip_reader_is_file_a_directory(
                    archive.get(), index) != 0;
                if (!directory && !mz_zip_reader_is_file_supported(
                        archive.get(), index))
                {
                    throw Error(
                        "The renderer package archive used unsupported compression or encryption.");
                }
                if (directory && !path.empty() && path.back() == '/')
                    path.pop_back();
                path = normalize_relative(path);
                if (!names.insert(path).second ||
                    (!directory && path != ManifestName &&
                        !is_allowed_file(path)) ||
                    (directory && !is_allowed_directory(path)))
                {
                    throw Error("The renderer package archive contained an unsafe or unexpected path '" +
                        path + "'.");
                }
                if (stat.m_uncomp_size > MaximumRendererExpandedBytes - expanded)
                    throw Error(
                        "The renderer package expanded size exceeded its safe limit.");
                expanded += stat.m_uncomp_size;
                result.push_back({ index, std::move(path), stat.m_uncomp_size,
                    directory });
            }
            if (!names.contains(ManifestName) || !names.contains(EnginePath))
                throw Error("The renderer package archive was incomplete.");
            return result;
        }

        struct ExtractTarget
        {
            HANDLE handle = INVALID_HANDLE_VALUE;
            std::uint64_t written = 0;
            DWORD error = ERROR_SUCCESS;
        };

        size_t zip_write(
            void* opaque,
            mz_uint64 offset,
            const void* buffer,
            size_t bytes)
        {
            auto& target = *static_cast<ExtractTarget*>(opaque);
            if (offset != target.written ||
                bytes > std::numeric_limits<DWORD>::max())
            {
                target.error = ERROR_INVALID_DATA;
                return 0;
            }
            DWORD written = 0;
            if (!WriteFile(target.handle, buffer, static_cast<DWORD>(bytes),
                    &written, nullptr) || written != bytes)
            {
                target.error = GetLastError();
                if (target.error == ERROR_SUCCESS)
                    target.error = ERROR_WRITE_FAULT;
                return 0;
            }
            target.written += written;
            return written;
        }

        void extract_archive(
            ZipArchive& archive,
            const fs::path& packageRoot,
            const std::vector<ArchiveEntry>& entries)
        {
            if (fs::exists(packageRoot))
                throw Error("The renderer package staging directory already existed.");
            fs::create_directories(packageRoot);
            reject_reparse_chain(packageRoot, "renderer package staging directory");
            for (const auto& entry : entries)
            {
                const fs::path destination = packageRoot / widen(entry.path);
                if (!is_strict_descendant(destination, packageRoot))
                    throw Error("A renderer package path escaped its staging root.");
                if (entry.directory)
                {
                    fs::create_directories(destination);
                    reject_reparse_chain(destination,
                        "renderer package directory");
                    continue;
                }
                fs::create_directories(destination.parent_path());
                reject_reparse_chain(destination.parent_path(),
                    "renderer package directory");
                ExtractTarget target;
                target.handle = CreateFileW(destination.c_str(), GENERIC_WRITE,
                    0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
                if (target.handle == INVALID_HANDLE_VALUE)
                    detail::throw_last_error("Windows could not create a staged package file");
                const bool extracted = mz_zip_reader_extract_to_callback(
                    archive.get(), entry.index, zip_write, &target, 0) != 0;
                const BOOL flushed = FlushFileBuffers(target.handle);
                CloseHandle(target.handle);
                target.handle = INVALID_HANDLE_VALUE;
                if (!extracted || target.error != ERROR_SUCCESS || !flushed ||
                    target.written != entry.size)
                {
                    throw Error("The renderer package could not be extracted safely.");
                }
            }
        }

        [[nodiscard]] PackageManifest extract_validate(
            const fs::path& archive,
            const fs::path& packageRoot,
            const RendererFeed& feed)
        {
            reject_reparse_chain(archive, "renderer package archive");
            ZipArchive inspection(archive);
            const auto size = fs::file_size(archive);
            if (size != feed.artifact.size ||
                !hash_equal(sha256(archive), feed.artifact.sha256))
            {
                throw Error(
                    "The renderer package did not match its signed feed identity.");
            }
            std::uint64_t expanded = 0;
            const auto entries = inspect_archive(inspection, expanded);
            const fs::path volume = packageRoot.root_path();
            const auto free = detail::available_bytes(
                volume.empty() ? packageRoot : volume);
            constexpr std::uint64_t reserve = 1024ull * 1024ull * 1024ull;
            if (expanded > std::numeric_limits<std::uint64_t>::max() - reserve ||
                free < expanded + reserve)
            {
                throw Error("The renderer package does not fit safely in staging.");
            }
            extract_archive(inspection, packageRoot, entries);
            PackageManifest manifest = read_package_manifest(packageRoot);
            if (manifest.releaseSequence != feed.releaseSequence ||
                manifest.sourceCommit != feed.sourceCommit ||
                manifest.settingsHash != feed.settingsHash ||
                manifest.engineVersion != feed.engineVersion)
            {
                throw Error(
                    "The renderer package metadata did not match its signed feed.");
            }
            validate_renderer_package(packageRoot, manifest);
            return manifest;
        }
    }

    PackageManifest read_package_manifest(const fs::path& packageRoot)
    {
        const fs::path path = packageRoot / fs::path(ManifestName);
        reject_reparse_chain(path, "renderer package manifest");
        return parse_manifest(detail::parse_json(detail::read_text(
            path, MaximumManifestBytes)));
    }

    void validate_renderer_package(
        const fs::path& packageRoot,
        const PackageManifest& manifest)
    {
        reject_reparse_tree(packageRoot);
        validate_manifest_contract(manifest);
        WindowsPathSet names;
        std::map<std::string, const PackageFile*, CaseInsensitiveLess> recorded;
        for (const auto& file : manifest.files)
        {
            const std::string normalized = normalize_relative(file.relativePath);
            if (normalized != file.relativePath || !is_allowed_file(normalized) ||
                !is_lower_hex(file.sha256, 64u) || !names.insert(normalized).second)
            {
                throw Error("The renderer package file inventory was invalid.");
            }
            recorded.emplace(normalized, &file);
        }
        const auto& required = inventory();
        if (!recorded.contains(EnginePath) ||
            !recorded.contains(D3d12CorePath) ||
            !recorded.contains(SettingsPath) ||
            std::count_if(recorded.begin(), recorded.end(), [](const auto& item)
                { return begins_with(item.first, "bin/shaders/"); }) !=
                    static_cast<std::ptrdiff_t>(required.shaders.size()))
        {
            throw Error("The renderer runtime package was incomplete.");
        }
        for (const auto& path : required.shaders)
            if (!recorded.contains(path))
                throw Error("The renderer runtime package was incomplete.");
        for (const auto& path : required.protectedPaths)
            if (!recorded.contains(path))
                throw Error("The renderer runtime package was incomplete.");

        const auto actual = actual_files(packageRoot);
        if (actual.size() != recorded.size())
            throw Error("The renderer package file inventory has changed.");
        for (const auto& file : actual)
        {
            const auto found = recorded.find(file.relativePath);
            if (found == recorded.end() ||
                found->second->relativePath != file.relativePath ||
                found->second->size != file.size ||
                !hash_equal(found->second->sha256, file.sha256))
            {
                throw Error("The renderer package file '" + file.relativePath +
                    "' failed its integrity check.");
            }
        }
        const auto executable = recorded.at(std::string(EnginePath));
        if (!hash_equal(executable->sha256, manifest.executableSha256))
            throw Error("The renderer executable failed its integrity check.");
        const fs::path engine = packageRoot / fs::path(std::string(EnginePath));
        if (detail::version_string(engine, L"ProductName") != "UVSR Engine" ||
            detail::version_string(engine, L"FileVersion") !=
                manifest.engineVersion ||
            detail::version_string(engine, L"ProductVersion") !=
                manifest.engineVersion + "+" + manifest.settingsHash)
        {
            throw Error(
                "The renderer executable metadata did not match the package settings identity.");
        }
        validate_settings(packageRoot, manifest);
    }

    PackageManifest stage_renderer_archive_for_test(
        const fs::path& archive,
        const fs::path& packageRoot,
        const RendererFeed& feed)
    {
        validate_renderer_feed(feed);
        return extract_validate(archive, packageRoot, feed);
    }

    namespace detail
    {
        PackageManifest stage_renderer_archive(
            const InstallerPaths& paths,
            const GUID& transactionId,
            const fs::path& archive,
            const RendererFeed& feed,
            InstallLog& log)
        {
            validate_renderer_feed(feed);
            const fs::path transactionRoot = paths.staging_directory() /
                widen(guid_string(transactionId));
            const fs::path packageRoot = transactionRoot / L"package";
            fs::create_directories(paths.staging_directory());
            reject_reparse_chain(paths.staging_directory(),
                "UVSR package staging directory");
            if (fs::exists(transactionRoot))
                delete_owned_tree(transactionRoot, paths.staging_directory());
            try
            {
                const PackageManifest manifest = extract_validate(
                    archive, packageRoot, feed);
                log.write("Staged signed renderer package sequence " +
                    std::to_string(feed.releaseSequence) + "; engine " +
                    manifest.engineVersion + "; settings " +
                    manifest.settingsHash + "; executable SHA-256 " +
                    manifest.executableSha256 + ".");
                return manifest;
            }
            catch (...)
            {
                if (fs::exists(transactionRoot))
                    delete_owned_tree(transactionRoot, paths.staging_directory());
                throw;
            }
        }

        fs::path activate_renderer_package(
            const InstallerPaths& paths,
            const GUID& transactionId,
            std::string_view versionId,
            const PackageManifest& manifest)
        {
            if (!is_version_id(versionId))
                throw Error("The renderer activation identity was invalid.");
            fs::create_directories(paths.versions_directory());
            reject_reparse_chain(paths.versions_directory(),
                "UVSR managed versions directory");
            const fs::path source = paths.staging_directory() /
                widen(guid_string(transactionId)) / L"package";
            const fs::path destination = paths.version_root(versionId);
            if (fs::exists(destination))
                throw Error(
                    "The candidate UVSR version already exists. No active version was changed.");
            fs::rename(source, destination);
            validate_renderer_package(destination, manifest);
            return destination;
        }
    }
}
