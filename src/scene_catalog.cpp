#include "scene_catalog.h"
#include "scene_catalog_path.h"
#include "json_document.h"
#include "file_bytes.h"
#include "settings_snapshot_storage.h"

#include <cctype>
#include <cmath>
#include <limits>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <wchar.h>

namespace
{
    using uvsr::SettingsSnapshotError;
    using uvsr::SettingsSnapshotErrorCode;
    namespace path = uvsr::catalog_path;
#if defined(UVSR_SCENE_CATALOG_TEST_HOOKS)
    thread_local size_t AllocationsBeforeFailure = SIZE_MAX;
#endif
    bool Fail(SettingsSnapshotError& error, SettingsSnapshotErrorCode code, const char* message, uint32_t native = 0) noexcept
    {
        error = {code, native, 0, message, {}};
        return false;
    }
    bool AllocateAllowed(SettingsSnapshotError& error) noexcept
    {
#if defined(UVSR_SCENE_CATALOG_TEST_HOOKS)
        if (AllocationsBeforeFailure == 0)
            return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not allocate the scene catalog.");
        if (AllocationsBeforeFailure != SIZE_MAX) --AllocationsBeforeFailure;
#else
        (void)error;
#endif
        return true;
    }
    bool PathFailure(const path::Error& failure, SettingsSnapshotError& error) noexcept
    {
        const auto code = failure.code == path::ErrorCode::Capacity ? SettingsSnapshotErrorCode::Capacity :
            failure.code == path::ErrorCode::InvalidInput ? SettingsSnapshotErrorCode::InvalidInput : SettingsSnapshotErrorCode::Path;
        return Fail(error, code, "Could not prepare a scene catalog path.", failure.nativeCode);
    }
    template<class T> struct Scratch
    {
        T* data = nullptr;
        size_t capacity = 0;
        ~Scratch() noexcept { free(data); }
        Scratch() noexcept = default;
        Scratch(const Scratch&) = delete;
        Scratch& operator=(const Scratch&) = delete;
        bool Reserve(size_t count, SettingsSnapshotError& error) noexcept
        {
            if (count <= capacity) return true;
            if (count > size_t(PTRDIFF_MAX) / sizeof(T))
                return Fail(error, SettingsSnapshotErrorCode::Capacity, "The scene catalog exceeds the addressable size.");
            if (!AllocateAllowed(error)) return false;
            void* candidate = realloc(data, count * sizeof(T));
            if (!candidate) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not allocate the scene catalog.");
            data = static_cast<T*>(candidate); capacity = count;
            return true;
        }
    };
    struct Wide : Scratch<wchar_t>
    {
        size_t size = 0;
        std::wstring_view View() const noexcept { return {data ? data : L"", size}; }
        bool Copy(std::wstring_view input, SettingsSnapshotError& error) noexcept
        {
            if ((!input.empty() && !input.data()) || input.size() > size_t(PTRDIFF_MAX) / sizeof(wchar_t) - 1 ||
                input.size() * sizeof(wchar_t) > UINTPTR_MAX - reinterpret_cast<uintptr_t>(input.data()))
                return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "The scene path range is invalid.");
            if (!Reserve(input.size() + 1, error)) return false;
            if (!input.empty()) wmemcpy(data, input.data(), input.size());
            data[input.size()] = L'\0'; size = input.size();
            return true;
        }
        bool Decode(std::string_view input, path::Encoding encoding, SettingsSnapshotError& error) noexcept
        {
            path::Error failure; path::ConversionPlan plan;
            if (!path::MeasureDecode(input, encoding, plan, failure)) return PathFailure(failure, error);
            if (!Reserve(plan.size + 1, error)) return false;
            if (!path::Decode(input, plan, data, capacity, size, failure)) return PathFailure(failure, error);
            return true;
        }
        void Normalize() noexcept { size = path::Normalize(data, size); }
        bool Join(std::wstring_view left, std::wstring_view right, SettingsSnapshotError& error) noexcept
        {
            path::Error failure; path::JoinPlan plan;
            if (!path::PlanJoin(left, right, plan, failure)) return PathFailure(failure, error);
            const bool leftIsSelf = left.data() == data;
            const size_t leftSize = left.size();
            if (!Reserve(plan.size + 1, error)) return false;
            if (leftIsSelf) left = {data, leftSize};
            if (!path::Join(left, right, data, capacity, size, failure)) return PathFailure(failure, error);
            return true;
        }
    };
    struct Text : Scratch<char>
    {
        size_t size = 0;
        std::string_view View() const noexcept { return {data ? data : "", size}; }
        bool Encode(std::wstring_view input, SettingsSnapshotError& error, path::Encoding encoding = path::Encoding::Filesystem) noexcept
        {
            path::Error failure; path::ConversionPlan plan;
            if (!path::MeasureEncode(input, plan, failure, encoding)) return PathFailure(failure, error);
            if (!Reserve(plan.size + 1, error)) return false;
            if (!path::Encode(input, plan, data, capacity, size, failure)) return PathFailure(failure, error);
            return true;
        }
    };
    struct TextRange { size_t offset = 0, size = 0; };
    struct Arena : Scratch<char>
    {
        size_t size = 0;
        std::string_view View(TextRange range) const noexcept { return {data + range.offset, range.size}; }
        bool Add(std::string_view text, TextRange& output, SettingsSnapshotError& error) noexcept
        {
            if (text.size() > size_t(PTRDIFF_MAX) - 1 - size)
                return Fail(error, SettingsSnapshotErrorCode::Capacity, "The scene catalog text exceeds the addressable size.");
            const size_t next = size + text.size() + 1;
            if (next > capacity)
            {
                const size_t grown = capacity <= size_t(PTRDIFF_MAX) / 2 ? capacity * 2 : size_t(PTRDIFF_MAX);
                if (!Reserve(next > grown ? next : grown, error)) return false;
            }
            if (!text.empty()) memcpy(data + size, text.data(), text.size());
            data[next - 1] = '\0'; output = {size, text.size()}; size = next;
            return true;
        }
    };
    int Compare(std::string_view left, std::string_view right) noexcept
    {
        const size_t count = left.size() < right.size() ? left.size() : right.size();
        for (size_t i = 0; i < count; ++i)
        {
            unsigned char a = static_cast<unsigned char>(left[i]), b = static_cast<unsigned char>(right[i]);
#ifdef _WIN32
            a = static_cast<unsigned char>(std::tolower(a)); b = static_cast<unsigned char>(std::tolower(b));
#endif
            if (a != b) return a < b ? -1 : 1;
        }
        return left.size() == right.size() ? 0 : left.size() < right.size() ? -1 : 1;
    }
    template<class T, class Less> void Sort(T* entries, size_t count, Less less) noexcept
    {
        const auto sift = [&](size_t root, size_t end) noexcept
        {
            while (root < end / 2)
            {
                size_t child = root * 2 + 1;
                if (child + 1 < end && less(entries[child], entries[child + 1])) ++child;
                if (!less(entries[root], entries[child])) break;
                T temporary = std::move(entries[root]); entries[root] = std::move(entries[child]); entries[child] = std::move(temporary);
                root = child;
            }
        };
        for (size_t i = count / 2; i > 0; --i) sift(i - 1, count);
        for (size_t end = count; end > 1; --end)
        {
            T temporary = std::move(entries[0]); entries[0] = std::move(entries[end - 1]); entries[end - 1] = std::move(temporary);
            sift(0, end - 1);
        }
    }
    bool Display(std::wstring_view directory, std::wstring_view filename, Text& output, SettingsSnapshotError& error) noexcept
    {
        Wide normalizedDirectory, normalizedFile, relative;
        if (!normalizedDirectory.Copy(directory, error) || !normalizedFile.Copy(filename, error)) return false;
        normalizedDirectory.Normalize(); normalizedFile.Normalize();
        auto dir = normalizedDirectory.View();
        if (path::RelativeStart(dir) < dir.size() && path::FilenameStart(dir) == dir.size())
        {
            normalizedDirectory.size = path::ParentLength(dir);
            normalizedDirectory.data[normalizedDirectory.size] = L'\0';
        }
        path::MakeGeneric(normalizedDirectory.data, normalizedDirectory.size);
        path::MakeGeneric(normalizedFile.data, normalizedFile.size);
        dir = normalizedDirectory.View(); const auto file = normalizedFile.View();
        size_t directoryCursor = 0, fileCursor = 0;
        std::wstring_view directoryComponent, fileComponent;
        Text a, b;
        while (path::Next(dir, directoryCursor, directoryComponent))
        {
            if (!path::Next(file, fileCursor, fileComponent)) return output.Encode(file, error);
            if (!a.Encode(directoryComponent, error) || !b.Encode(fileComponent, error)) return false;
            if (Compare(a.View(), b.View()) != 0) return output.Encode(file, error);
        }
        while (path::Next(file, fileCursor, fileComponent))
            if (!relative.Join(relative.View(), fileComponent, error)) return false;
        if (!relative.size) return output.Encode(file, error);
        path::MakeGeneric(relative.data, relative.size);
        return output.Encode(relative.View(), error);
    }
    bool EndsWithCaseInsensitive(std::string_view value, std::string_view suffix)
    {
        if (suffix.size() > value.size())
            return false;

        const size_t offset = value.size() - suffix.size();
        for (size_t index = 0; index < suffix.size(); ++index)
        {
            const unsigned char left = static_cast<unsigned char>(value[offset + index]);
            const unsigned char right = static_cast<unsigned char>(suffix[index]);
            if (std::tolower(left) != std::tolower(right))
                return false;
        }
        return true;
    }

    bool IsDescriptor(std::string_view fileName)
    {
        return EndsWithCaseInsensitive(fileName, ".scene.json");
    }

    bool HasVisibleText(std::string_view text)
    {
        for (unsigned char character : text) if (!std::isspace(character)) return true;
        return false;
    }

    std::optional<std::array<float, 3>> ReadFiniteFloat3(
        uvsr::json::Value value)
    {
        using Kind = uvsr::json::Kind;
        if (!value.IsValid() || value.Type() != Kind::Array || value.Count() != 3u)
        {
            return std::nullopt;
        }

        std::array<float, 3> result{};
        for (size_t index = 0u; index < 3u; ++index)
        {
            const uvsr::json::Value component = value.At(index);
            if (component.Type() != Kind::Number ||
                component.Number() < -double(std::numeric_limits<float>::max()) ||
                component.Number() > double(std::numeric_limits<float>::max()))
            {
                return std::nullopt;
            }

            result[index] = static_cast<float>(component.Number());
            if (!std::isfinite(result[index]))
                return std::nullopt;
        }
        return result;
    }

    double LengthSquared(const std::array<float, 3>& value)
    {
        return double(value[0]) * double(value[0]) +
            double(value[1]) * double(value[1]) +
            double(value[2]) * double(value[2]);
    }

    std::optional<std::array<float, 3>> NormalizeCameraVector(
        const std::array<float, 3>& value)
    {
        const double lengthSquared = LengthSquared(value);
        if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-8)
            return std::nullopt;

        const double inverseLength = 1.0 / std::sqrt(lengthSquared);
        return std::array<float, 3>{
            float(double(value[0]) * inverseLength),
            float(double(value[1]) * inverseLength),
            float(double(value[2]) * inverseLength)
        };
    }

    double CrossLengthSquared(
        const std::array<float, 3>& left,
        const std::array<float, 3>& right)
    {
        const double x =
            double(left[1]) * double(right[2]) -
            double(left[2]) * double(right[1]);
        const double y =
            double(left[2]) * double(right[0]) -
            double(left[0]) * double(right[2]);
        const double z =
            double(left[0]) * double(right[1]) -
            double(left[1]) * double(right[0]);
        return x * x + y * y + z * z;
    }

    std::optional<uvsr::SceneInitialCamera> ReadInitialCamera(
        uvsr::json::Value document)
    {
        using Kind = uvsr::json::Kind;
        const uvsr::json::Value camera = document.Find("initialCamera");
        if (!camera.IsValid() || camera.Type() != Kind::Object)
            return std::nullopt;

        const auto position = ReadFiniteFloat3(camera.Find("position"));
        const auto direction = ReadFiniteFloat3(camera.Find("direction"));
        const auto up = ReadFiniteFloat3(camera.Find("up"));
        const auto normalizedDirection =
            direction ? NormalizeCameraVector(*direction) : std::nullopt;
        const auto normalizedUp =
            up ? NormalizeCameraVector(*up) : std::nullopt;
        if (!position || !normalizedDirection || !normalizedUp ||
            CrossLengthSquared(*normalizedDirection, *normalizedUp) <= 1e-8)
        {
            return std::nullopt;
        }

        float verticalFovDegrees = 60.f;
        const uvsr::json::Value verticalFov = camera.Find("verticalFovDegrees");
        if (verticalFov.IsValid() && verticalFov.Type() != Kind::Null)
        {
            if (verticalFov.Type() != Kind::Number ||
                verticalFov.Number() < -double(std::numeric_limits<float>::max()) ||
                verticalFov.Number() > double(std::numeric_limits<float>::max()))
            {
                return std::nullopt;
            }
            verticalFovDegrees = static_cast<float>(verticalFov.Number());
            if (!std::isfinite(verticalFovDegrees) ||
                verticalFovDegrees <= 1.f ||
                verticalFovDegrees >= 179.f)
            {
                return std::nullopt;
            }
        }

        return uvsr::SceneInitialCamera{
            *position,
            *normalizedDirection,
            *normalizedUp,
            verticalFovDegrees
        };
    }}

namespace uvsr
{
    SceneCatalog::~SceneCatalog() noexcept { Clear(); }
    SceneCatalog::SceneCatalog(SceneCatalog&& other) noexcept
        : m_Entries(other.m_Entries), m_Text(other.m_Text), m_Count(other.m_Count)
    {
        other.m_Entries = nullptr; other.m_Text = nullptr; other.m_Count = 0;
    }
    SceneCatalog& SceneCatalog::operator=(SceneCatalog&& other) noexcept
    {
        if (this != &other)
        {
            Clear(); m_Entries = other.m_Entries; m_Text = other.m_Text; m_Count = other.m_Count;
            other.m_Entries = nullptr; other.m_Text = nullptr; other.m_Count = 0;
        }
        return *this;
    }
    void SceneCatalog::Clear() noexcept
    {
        delete[] m_Entries; free(m_Text);
        m_Entries = nullptr; m_Text = nullptr; m_Count = 0;
    }
    struct SceneCatalogBuilder
    {
        struct Work
        {
            TextRange file, command, label;
            std::optional<SceneInitialCamera> camera;
            size_t canonical = 0;
            bool descriptor = false, component = false, hasLabel = false;
        };
        Work* work = nullptr;
        size_t* order = nullptr;
        size_t count = 0;
        Arena arena;
        ~SceneCatalogBuilder() noexcept { delete[] work; delete[] order; }
        bool Prepare(size_t size, SettingsSnapshotError& error) noexcept
        {
            if (!size) return true;
            if (size > size_t(PTRDIFF_MAX) / sizeof(Work) || size > size_t(PTRDIFF_MAX) / sizeof(size_t))
                return Fail(error, SettingsSnapshotErrorCode::Capacity, "The scene catalog exceeds the addressable size.");
            if (!AllocateAllowed(error)) return false;
            work = new(std::nothrow) Work[size];
            if (!work) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not allocate the scene catalog.");
            if (!AllocateAllowed(error)) return false;
            order = new(std::nothrow) size_t[size];
            if (!order) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not allocate the scene catalog.");
            count = size;
            return true;
        }
        size_t Find(std::string_view file) const noexcept
        {
            size_t first = 0, end = count;
            while (first < end)
            {
                const size_t middle = first + (end - first) / 2;
                if (Compare(arena.View(work[order[middle]].file), file) < 0) first = middle + 1;
                else end = middle;
            }
            return first < count && Compare(arena.View(work[order[first]].file), file) == 0 ? order[first] : count;
        }
        bool Build(std::wstring_view directory, ArrayView<const std::string_view> discovered,
            SceneCatalog& output, SettingsSnapshotError& error) noexcept
        {
            if (!discovered.IsValid() || discovered.count > size_t(PTRDIFF_MAX) / sizeof(std::string_view) ||
                discovered.count * sizeof(std::string_view) > UINTPTR_MAX - reinterpret_cast<uintptr_t>(discovered.data))
                return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "The scene discovery range is invalid.");
            if (!Prepare(discovered.count, error)) return false;
            Wide native, model, joined;
            Text encoded, command;
            for (size_t i = 0; i < count; ++i)
            {
                if (!native.Decode(discovered.data[i], path::Encoding::Filesystem, error)) return false;
                native.Normalize(); path::MakeGeneric(native.data, native.size);
                if (!encoded.Encode(native.View(), error) || !arena.Add(encoded.View(), work[i].file, error)) return false;
                work[i].descriptor = IsDescriptor(encoded.View());
                order[i] = i;
            }
            Sort(order, count, [&](size_t a, size_t b) noexcept {
                const int comparison = Compare(arena.View(work[a].file), arena.View(work[b].file));
                return comparison ? comparison < 0 : a < b;
            });
            size_t canonical = 0;
            for (size_t i = 0; i < count; ++i)
            {
                if (!i || Compare(arena.View(work[order[i]].file), arena.View(work[canonical].file)) != 0) canonical = order[i];
                work[order[i]].canonical = canonical;
            }
            for (size_t i = 0; i < count; ++i)
            {
                if (!work[i].descriptor) continue;
                if (!native.Decode(arena.View(work[i].file), path::Encoding::Filesystem, error)) return false;
                FileBytes bytes; FileReadResult read;
                if (!ReadFileBytes(native.data, 16u * 1024u * 1024u, bytes, read))
                {
                    if (read.error == FileReadError::OutOfMemory)
                        return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not read scene catalog metadata.", read.systemCode);
                    continue;
                }
                json::Document owner; json::Error failure;
                if (!owner.Parse({bytes.Data(), bytes.Size()}, failure))
                {
                    if (failure.code == json::ErrorCode::OutOfMemory)
                        return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not parse scene catalog metadata.");
                    continue;
                }
                const json::Value document = owner.Root();
                if (document.Type() != json::Kind::Object) continue;
                Work& target = work[work[i].canonical];
                const json::Value label = document.Find("displayName");
                if (!target.hasLabel && label.Type() == json::Kind::String && HasVisibleText({label.Text().data, label.Text().size}))
                {
                    if (!arena.Add({label.Text().data, label.Text().size}, target.label, error)) return false;
                    target.hasLabel = true;
                }
                if (!target.camera) target.camera = ReadInitialCamera(document);
                const json::Value models = document.Find("models");
                if (models.Type() != json::Kind::Array) continue;
                const auto parent = native.View().substr(0, path::ParentLength(native.View()));
                for (json::Value value = models.First(); value.IsValid(); value = value.Next())
                {
                    if (value.Type() != json::Kind::String || !value.Text().size) continue;
                    if (!model.Decode({value.Text().data, value.Text().size}, path::Encoding::Utf8, error) ||
                        !joined.Join(parent, model.View(), error)) return false;
                    joined.Normalize(); path::MakeGeneric(joined.data, joined.size);
                    if (!encoded.Encode(joined.View(), error)) return false;
                    const size_t match = Find(encoded.View());
                    if (match < count) work[match].component = true;
                }
            }
            size_t entries = 0, textBytes = 0;
            for (size_t i = 0; i < count; ++i)
            {
                Work& row = work[i];
                if (row.canonical != i || (!row.descriptor && row.component)) continue;
                if (!native.Decode(arena.View(row.file), path::Encoding::Filesystem, error) ||
                    !Display(directory, native.View(), command, error) || !arena.Add(command.View(), row.command, error)) return false;
                const size_t sizes[]{row.file.size, row.command.size, row.hasLabel ? row.label.size : 0};
                const size_t fields = row.hasLabel ? 3 : 2;
                for (size_t field = 0; field < fields; ++field)
                {
                    if (sizes[field] > size_t(PTRDIFF_MAX) - 1 - textBytes)
                        return Fail(error, SettingsSnapshotErrorCode::Capacity, "The scene catalog text exceeds the addressable size.");
                    textBytes += sizes[field] + 1;
                }
                ++entries;
            }
            SceneCatalog candidate;
            if (entries)
            {
                if (entries > size_t(PTRDIFF_MAX) / sizeof(SceneCatalogEntry))
                    return Fail(error, SettingsSnapshotErrorCode::Capacity, "The scene catalog exceeds the addressable size.");
                if (!AllocateAllowed(error)) return false;
                candidate.m_Entries = new(std::nothrow) SceneCatalogEntry[entries];
                if (!candidate.m_Entries) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not allocate the scene catalog.");
                if (!AllocateAllowed(error)) return false;
                candidate.m_Text = static_cast<char*>(malloc(textBytes));
                if (!candidate.m_Text) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not allocate the scene catalog.");
            }
            size_t offset = 0;
            const auto store = [&](TextRange range) noexcept -> std::string_view {
                char* destination = candidate.m_Text + offset;
                memcpy(destination, arena.data + range.offset, range.size + 1);
                offset += range.size + 1;
                return {destination, range.size};
            };
            for (size_t i = 0; i < count; ++i)
            {
                const Work& row = work[i];
                if (row.canonical != i || (!row.descriptor && row.component)) continue;
                auto& entry = candidate.m_Entries[candidate.m_Count++];
                entry.FileName = store(row.file); entry.CommandName = store(row.command);
                entry.DisplayName = row.hasLabel ? store(row.label) : entry.CommandName;
                entry.InitialCamera = row.camera;
            }
            Sort(candidate.m_Entries, candidate.m_Count, [](const SceneCatalogEntry& a, const SceneCatalogEntry& b) noexcept {
                const int label = Compare(a.DisplayName, b.DisplayName);
                return label ? label < 0 : Compare(a.FileName, b.FileName) < 0;
            });
            output = std::move(candidate);
            return true;
        }
    };
    bool BuildSceneCatalog(std::wstring_view directory, ArrayView<const std::string_view> discovered,
        SceneCatalog& output, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail));
        error = {};
        SceneCatalogBuilder builder;
        return builder.Build(directory, discovered, output, error);
    }
    bool MakeSceneDisplayName(std::wstring_view directory, std::wstring_view filename,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept
    {
        if (&output == &error.detail) return false;
        json::EncodedText previousDetail(std::move(error.detail));
        error = {};
        Text text;
        if (!Display(directory, filename, text, error)) return false;
        const auto view = text.View();
        json::EncodedText candidate([](json::OutputWriter& writer, const void* context) noexcept {
            const auto value = *static_cast<const std::string_view*>(context);
            return writer.Raw({value.data(), value.size()});
        }, &view);
        if (!candidate.IsValid())
        {
            const auto failure = candidate.Failure();
            return Fail(error, failure.code == json::ErrorCode::OutOfMemory ? SettingsSnapshotErrorCode::OutOfMemory :
                failure.code == json::ErrorCode::Capacity ? SettingsSnapshotErrorCode::Capacity : SettingsSnapshotErrorCode::InvalidInput,
                failure.message);
        }
        output = std::move(candidate);
        return true;
    }
    bool MakeSceneDisplayName(std::wstring_view directory, std::string_view filename,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept
    {
        if (&output == &error.detail) return false;
        json::EncodedText previousDetail(std::move(error.detail));
        error = {};
        Wide native;
        if (!native.Decode(filename, path::Encoding::Filesystem, error)) return false;
        return MakeSceneDisplayName(directory, native.View(), output, error);
    }
    bool FindSceneCatalogEntry(const SceneCatalog& catalog, std::string_view filename,
        const SceneCatalogEntry*& output, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail));
        error = {};
        Wide native; Text encoded;
        if (!native.Decode(filename, path::Encoding::Filesystem, error)) return false;
        native.Normalize(); path::MakeGeneric(native.data, native.size);
        if (!encoded.Encode(native.View(), error)) return false;
        for (const SceneCatalogEntry& entry : catalog)
        {
            if (Compare(entry.FileName, encoded.View()) != 0) continue;
            output = &entry;
            return true;
        }
        output = nullptr;
        return true;
    }
    SceneLoadRequest::~SceneLoadRequest() noexcept { Clear(); }
    SceneLoadRequest::SceneLoadRequest(SceneLoadRequest&& other) noexcept
        : m_Text(other.m_Text), m_FileName(other.m_FileName), m_DisplayName(other.m_DisplayName),
          m_ImportFileName(other.m_ImportFileName), m_Entry(other.m_Entry)
    {
        other.m_Text = nullptr; other.m_FileName = ""; other.m_DisplayName = ""; other.m_ImportFileName = ""; other.m_Entry = nullptr;
    }
    SceneLoadRequest& SceneLoadRequest::operator=(SceneLoadRequest&& other) noexcept
    {
        if (this != &other)
        {
            Clear(); m_Text = other.m_Text; m_FileName = other.m_FileName; m_DisplayName = other.m_DisplayName;
            m_ImportFileName = other.m_ImportFileName; m_Entry = other.m_Entry;
            other.m_Text = nullptr; other.m_FileName = ""; other.m_DisplayName = ""; other.m_ImportFileName = ""; other.m_Entry = nullptr;
        }
        return *this;
    }
    void SceneLoadRequest::Clear() noexcept
    {
        free(m_Text); m_Text = nullptr;
        m_FileName = ""; m_DisplayName = ""; m_ImportFileName = ""; m_Entry = nullptr;
    }
    bool PrepareSceneLoadRequest(std::wstring_view directory, std::string_view filename,
        const SceneCatalogEntry* entry, SceneLoadRequest& output, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail)); error = {};
        const std::string_view resolved = entry ? entry->FileName : filename;
        Wide native; Text label, imported;
        if (!native.Decode(resolved, path::Encoding::Filesystem, error)) return false;
        if (!entry && !Display(directory, native.View(), label, error)) return false;
        if (!imported.Encode(native.View(), error, path::Encoding::Utf8)) return false;
        size_t bytes = imported.size + 1;
        if (!entry)
        {
            for (size_t size : {resolved.size(), label.size})
            {
                if (size > size_t(PTRDIFF_MAX) - 1 - bytes)
                    return Fail(error, SettingsSnapshotErrorCode::Capacity, "The scene request exceeds the addressable size.");
                bytes += size + 1;
            }
        }
        SceneLoadRequest candidate;
        if (!AllocateAllowed(error)) return false;
        candidate.m_Text = static_cast<char*>(malloc(bytes));
        if (!candidate.m_Text) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not allocate the scene request.");
        size_t offset = 0;
        const auto store = [&](std::string_view text) noexcept -> std::string_view {
            char* destination = candidate.m_Text + offset;
            if (!text.empty()) memcpy(destination, text.data(), text.size());
            destination[text.size()] = '\0'; offset += text.size() + 1;
            return {destination, text.size()};
        };
        candidate.m_Entry = entry;
        candidate.m_ImportFileName = store(imported.View());
        candidate.m_FileName = entry ? entry->FileName : store(resolved);
        candidate.m_DisplayName = entry ? entry->DisplayName : store(label.View());
        output = std::move(candidate);
        return true;
    }
#if defined(UVSR_SCENE_CATALOG_TEST_HOOKS)
    void FailSceneCatalogAllocationAfter(size_t count) noexcept { AllocationsBeforeFailure = count; }
    void ClearSceneCatalogAllocationFailure() noexcept { AllocationsBeforeFailure = SIZE_MAX; }
#endif
}
