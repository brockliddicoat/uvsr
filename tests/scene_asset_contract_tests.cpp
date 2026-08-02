#include "scene_catalog.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <donut/core/json.h>
#include <donut/core/vfs/VFS.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // GitHub rejects a blob at or above 100,000,000 decimal bytes. Keeping the
    // strict comparison here protects both source assets and the exact runtime
    // copies produced by CMake.
    constexpr uint64_t kMaximumTrackedFileBytes = 100'000'000ull;
    constexpr double kPi = 3.14159265358979323846;

    struct Float3
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
    };

    struct ExpectedCamera
    {
        std::array<float, 3> Position{};
        std::array<float, 3> Direction{ 0.f, 0.f, -1.f };
        std::array<float, 3> Up{ 0.f, 1.f, 0.f };
        float VerticalFovDegrees = 60.f;
    };

    struct ExpectedDescriptor
    {
        const char* RelativePath = nullptr;
        const char* DisplayName = nullptr;
        size_t ModelCount = 0u;
    };

    struct ExpectedDownloadedScene
    {
        const char* DescriptorRelativePath = nullptr;
        const char* ModelRelativePath = nullptr;
        ExpectedCamera Camera;
        std::array<float, 3> EmbeddedCameraPositionOffset{};
        size_t MinimumExternalBufferCount = 1u;
        size_t MinimumHorizontalEnclosureRayCount = 4u;
        bool ExpectTransformedMeshNodes = false;
        bool CompareEmbeddedCamera = false;
    };

    constexpr std::array<ExpectedDescriptor, 5> kExpectedDescriptors = {{
        {
            "intel_sponza/intel_pbr_sponza.scene.json",
            "Sponza Decorated",
            5u
        },
        {
            "intel_sponza/intel_pbr_sponza_plain.scene.json",
            "Sponza Plain",
            2u
        },
        {
            "bistro_interior_retextured/bistro_interior_retextured.scene.json",
            "Bistro Interior",
            1u
        },
        {
            "san_miguel_retextured/san_miguel_retextured.scene.json",
            "San Miguel",
            1u
        },
        {
            "blender_classroom/blender_classroom.scene.json",
            "Classroom Interior",
            1u
        },
    }};

    constexpr std::array<ExpectedDownloadedScene, 3> kDownloadedScenes = {{
        {
            "bistro_interior_retextured/bistro_interior_retextured.scene.json",
            "components/bistro_interior.gltf",
            {
                { 4.444546f, 2.258351f, -2.746721f },
                { 0.992681f, -0.037313f, -0.114857f },
                { 0.037065f, 0.999304f, -0.004289f },
                33.9666f
            },
            { 1.f, 0.f, -0.5f },
            2u,
            4u,
            true,
            true
        },
        {
            "san_miguel_retextured/san_miguel_retextured.scene.json",
            "components/san_miguel.gltf",
            {
                { 27.6255f, 1.49616f, 2.42353f },
                { -0.9673232088f, -0.0081301951f, -0.2534160802f },
                { -0.0078644596f, 0.9999669523f, -0.0020602299f },
                57.2209f
            },
            { 0.f, 0.f, 0.f },
            2u,
            4u,
            false,
            false
        },
        {
            "blender_classroom/blender_classroom.scene.json",
            "components/blender_classroom.gltf",
            {
                { 2.5133827f, 1.0972751f, 4.2238383f },
                { -0.2520502f, 0.0111987f, -0.9676494f },
                { 0.0028260f, 0.9999372f, 0.0108363f },
                39.5977509f
            },
            { -0.0630126f, 0.0027997f, -0.2419124f },
            1u,
            3u,
            true,
            true
        },
    }};

    constexpr std::array<const char*, 4> kSupportedSceneDirectories = {{
        "intel_sponza",
        "bistro_interior_retextured",
        "san_miguel_retextured",
        "blender_classroom",
    }};

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    std::string Generic(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    bool EndsWithCaseInsensitive(
        std::string_view value,
        std::string_view suffix)
    {
        if (suffix.size() > value.size())
            return false;

        const size_t offset = value.size() - suffix.size();
        for (size_t index = 0u; index < suffix.size(); ++index)
        {
            const unsigned char left =
                static_cast<unsigned char>(value[offset + index]);
            const unsigned char right =
                static_cast<unsigned char>(suffix[index]);
            if (std::tolower(left) != std::tolower(right))
                return false;
        }
        return true;
    }

    bool IsLoadableSceneFile(const std::filesystem::path& path)
    {
        const std::string name = path.generic_string();
        return EndsWithCaseInsensitive(name, ".scene.json") ||
            EndsWithCaseInsensitive(name, ".gltf") ||
            EndsWithCaseInsensitive(name, ".glb");
    }

    bool IsContainedBy(
        const std::filesystem::path& root,
        const std::filesystem::path& candidate)
    {
        const std::filesystem::path relative =
            candidate.lexically_normal().lexically_relative(
                root.lexically_normal());
        if (relative.empty() || relative.is_absolute())
            return false;
        return *relative.begin() != "..";
    }

    void RequireRegularFileBelowLimit(
        const std::filesystem::path& path,
        const std::string& context)
    {
        Require(std::filesystem::is_regular_file(path),
            context + " is missing: " + Generic(path));
        const uint64_t fileBytes = std::filesystem::file_size(path);
        Require(fileBytes < kMaximumTrackedFileBytes,
            context + " reaches or exceeds the 100,000,000-byte GitHub "
                "limit: " + Generic(path));
    }

    void RequireFilesEqual(
        const std::filesystem::path& source,
        const std::filesystem::path& staged)
    {
        RequireRegularFileBelowLimit(source, "source scene asset");
        RequireRegularFileBelowLimit(staged, "staged scene asset");
        Require(std::filesystem::file_size(source) ==
                std::filesystem::file_size(staged),
            "staged scene asset size differs: " + Generic(staged));

        std::ifstream left(source, std::ios::binary);
        std::ifstream right(staged, std::ios::binary);
        Require(left.good() && right.good(),
            "failed to open staged comparison inputs");
        // Keep the streaming buffers off the Windows worker-thread stack.
        // Two one-megabyte std::arrays exceed the default stack reservation
        // before the test reaches cgltf or any camera geometry.
        std::vector<char> leftBytes(1024u * 1024u);
        std::vector<char> rightBytes(1024u * 1024u);
        for (;;)
        {
            left.read(leftBytes.data(),
                static_cast<std::streamsize>(leftBytes.size()));
            right.read(rightBytes.data(),
                static_cast<std::streamsize>(rightBytes.size()));
            const std::streamsize leftCount = left.gcount();
            const std::streamsize rightCount = right.gcount();
            Require(leftCount == rightCount,
                "staged scene asset byte count differs: " + Generic(staged));
            Require(std::equal(
                    leftBytes.begin(),
                    leftBytes.begin() + leftCount,
                    rightBytes.begin()),
                "staged scene asset bytes differ: " + Generic(staged));
            if (leftCount == 0)
                break;
        }
    }

    std::set<std::string> CollectSceneTreeFiles(
        const std::filesystem::path& sceneDirectory)
    {
        Require(std::filesystem::is_directory(sceneDirectory),
            "scene asset directory is missing: " + Generic(sceneDirectory));

        std::set<std::string> result;
        for (const auto& item :
            std::filesystem::recursive_directory_iterator(sceneDirectory))
        {
            Require(!item.is_symlink(),
                "scene packages must not contain symlinks: " +
                    Generic(item.path()));
            if (!item.is_regular_file())
                continue;
            if (item.path().filename() == ".uvsr-stage.stamp")
                continue;

            RequireRegularFileBelowLimit(item.path(), "scene package file");
            const std::filesystem::path relative =
                item.path().lexically_normal().lexically_relative(
                    sceneDirectory.lexically_normal());
            Require(!relative.empty() && !relative.is_absolute(),
                "failed to make scene package path relative: " +
                    Generic(item.path()));
            Require(result.insert(relative.generic_string()).second,
                "duplicate scene package path: " + relative.generic_string());
        }
        Require(!result.empty(),
            "scene asset directory is empty: " + Generic(sceneDirectory));
        return result;
    }

    void ValidateStagedSceneTree(
        const std::filesystem::path& sourceRoot,
        const std::filesystem::path& stagingRoot,
        const std::filesystem::path& relativeSceneDirectory)
    {
        const std::filesystem::path sourceDirectory =
            sourceRoot / relativeSceneDirectory;
        const std::filesystem::path stagedDirectory =
            stagingRoot / relativeSceneDirectory;
        const std::set<std::string> sourceFiles =
            CollectSceneTreeFiles(sourceDirectory);
        const std::set<std::string> stagedFiles =
            CollectSceneTreeFiles(stagedDirectory);
        Require(sourceFiles == stagedFiles,
            "staged scene inventory differs for " +
                relativeSceneDirectory.generic_string());
        for (const std::string& relativeFile : sourceFiles)
        {
            RequireFilesEqual(
                sourceDirectory / relativeFile,
                stagedDirectory / relativeFile);
        }
    }

    int HexDigitValue(char character)
    {
        if (character >= '0' && character <= '9')
            return character - '0';
        if (character >= 'a' && character <= 'f')
            return character - 'a' + 10;
        if (character >= 'A' && character <= 'F')
            return character - 'A' + 10;
        return -1;
    }

    std::string DecodeRelativeUri(
        std::string_view uri,
        const std::string& context)
    {
        Require(!uri.empty(), context + " has an empty URI");
        Require(uri.find("://") == std::string_view::npos &&
                uri.rfind("data:", 0u) != 0u,
            context + " must use a local file URI");
        Require(uri.find('?') == std::string_view::npos &&
                uri.find('#') == std::string_view::npos &&
                uri.find('\\') == std::string_view::npos,
            context + " must use a plain forward-slash file URI");

        std::string decoded;
        decoded.reserve(uri.size());
        for (size_t index = 0u; index < uri.size(); ++index)
        {
            if (uri[index] != '%')
            {
                decoded.push_back(uri[index]);
                continue;
            }

            Require(index + 2u < uri.size(),
                context + " contains a truncated percent escape");
            const int high = HexDigitValue(uri[index + 1u]);
            const int low = HexDigitValue(uri[index + 2u]);
            Require(high >= 0 && low >= 0,
                context + " contains an invalid percent escape");
            const char decodedByte = static_cast<char>((high << 4) | low);
            Require(decodedByte != '\0',
                context + " contains an encoded NUL byte");
            decoded.push_back(decodedByte);
            index += 2u;
        }
        return decoded;
    }

    std::filesystem::path ResolveExternalResource(
        const std::filesystem::path& ownerFile,
        const std::filesystem::path& sceneRoot,
        std::string_view uri,
        const std::string& context)
    {
        const std::filesystem::path relative(
            DecodeRelativeUri(uri, context));
        Require(!relative.empty() && !relative.is_absolute() &&
                !relative.has_root_name() && !relative.has_root_directory(),
            context + " must be relative");
        const std::filesystem::path candidate =
            (ownerFile.parent_path() / relative).lexically_normal();
        Require(IsContainedBy(sceneRoot, candidate),
            context + " escapes the supported scene root");
        RequireRegularFileBelowLimit(candidate, context);

        // Lexical containment blocks ordinary ../ escapes. Canonical
        // containment additionally prevents an on-disk junction or symlink
        // from redirecting a staged resource outside the package.
        const std::filesystem::path canonicalRoot =
            std::filesystem::canonical(sceneRoot);
        const std::filesystem::path canonicalCandidate =
            std::filesystem::canonical(candidate);
        Require(IsContainedBy(canonicalRoot, canonicalCandidate),
            context + " resolves outside the supported scene root");
        return candidate;
    }

    std::array<float, 3> ReadFloat3(
        const Json::Value& value,
        const std::string& context)
    {
        Require(value.isArray() && value.size() == 3u,
            context + " must contain three numbers");
        std::array<float, 3> result{};
        for (Json::ArrayIndex index = 0u; index < 3u; ++index)
        {
            Require(value[index].isNumeric(),
                context + " contains a non-number");
            result[index] = value[index].asFloat();
            Require(std::isfinite(result[index]),
                context + " contains a non-finite number");
        }
        return result;
    }

    bool NearlyEqual(double left, double right, double tolerance = 1e-5)
    {
        return std::abs(left - right) <= tolerance;
    }

    bool NearlyEqual(
        const std::array<float, 3>& left,
        const std::array<float, 3>& right,
        double tolerance = 1e-5)
    {
        return NearlyEqual(left[0], right[0], tolerance) &&
            NearlyEqual(left[1], right[1], tolerance) &&
            NearlyEqual(left[2], right[2], tolerance);
    }

    Float3 ToFloat3(const std::array<float, 3>& value)
    {
        return { value[0], value[1], value[2] };
    }

    Float3 Add(Float3 left, Float3 right)
    {
        return {
            left.X + right.X,
            left.Y + right.Y,
            left.Z + right.Z
        };
    }

    Float3 Subtract(Float3 left, Float3 right)
    {
        return {
            left.X - right.X,
            left.Y - right.Y,
            left.Z - right.Z
        };
    }

    Float3 Multiply(Float3 value, double scalar)
    {
        return {
            value.X * scalar,
            value.Y * scalar,
            value.Z * scalar
        };
    }

    double Dot(Float3 left, Float3 right)
    {
        return left.X * right.X +
            left.Y * right.Y +
            left.Z * right.Z;
    }

    Float3 Cross(Float3 left, Float3 right)
    {
        return {
            left.Y * right.Z - left.Z * right.Y,
            left.Z * right.X - left.X * right.Z,
            left.X * right.Y - left.Y * right.X
        };
    }

    double LengthSquared(Float3 value)
    {
        return Dot(value, value);
    }

    Float3 Normalize(Float3 value, const std::string& context)
    {
        const double lengthSquared = LengthSquared(value);
        Require(std::isfinite(lengthSquared) && lengthSquared > 1e-12,
            context + " must be finite and nonzero");
        return Multiply(value, 1.0 / std::sqrt(lengthSquared));
    }

    bool IsFinite(Float3 value)
    {
        return std::isfinite(value.X) &&
            std::isfinite(value.Y) &&
            std::isfinite(value.Z);
    }

    Float3 TransformPoint(const cgltf_float* matrix, Float3 point)
    {
        return {
            matrix[0] * point.X + matrix[4] * point.Y +
                matrix[8] * point.Z + matrix[12],
            matrix[1] * point.X + matrix[5] * point.Y +
                matrix[9] * point.Z + matrix[13],
            matrix[2] * point.X + matrix[6] * point.Y +
                matrix[10] * point.Z + matrix[14]
        };
    }

    Float3 TransformVector(const cgltf_float* matrix, Float3 vector)
    {
        return {
            matrix[0] * vector.X + matrix[4] * vector.Y +
                matrix[8] * vector.Z,
            matrix[1] * vector.X + matrix[5] * vector.Y +
                matrix[9] * vector.Z,
            matrix[2] * vector.X + matrix[6] * vector.Y +
                matrix[10] * vector.Z
        };
    }

    bool IsIdentityMatrix(const cgltf_float* matrix)
    {
        constexpr std::array<float, 16> identity = {{
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f,
        }};
        for (size_t index = 0u; index < identity.size(); ++index)
        {
            if (!NearlyEqual(matrix[index], identity[index], 1e-6))
                return false;
        }
        return true;
    }

    double PointSegmentDistanceSquared(
        Float3 point,
        Float3 start,
        Float3 end)
    {
        const Float3 segment = Subtract(end, start);
        const double segmentLengthSquared = LengthSquared(segment);
        if (segmentLengthSquared <= 1e-18)
            return LengthSquared(Subtract(point, start));
        const double fraction = std::clamp(
            Dot(Subtract(point, start), segment) / segmentLengthSquared,
            0.0,
            1.0);
        return LengthSquared(Subtract(
            point,
            Add(start, Multiply(segment, fraction))));
    }

    double PointTriangleDistanceSquared(
        Float3 point,
        Float3 first,
        Float3 second,
        Float3 third)
    {
        const Float3 firstSecond = Subtract(second, first);
        const Float3 firstThird = Subtract(third, first);
        if (LengthSquared(Cross(firstSecond, firstThird)) <= 1e-18)
        {
            return std::min({
                PointSegmentDistanceSquared(point, first, second),
                PointSegmentDistanceSquared(point, second, third),
                PointSegmentDistanceSquared(point, third, first)
            });
        }

        const Float3 firstPoint = Subtract(point, first);
        const double d1 = Dot(firstSecond, firstPoint);
        const double d2 = Dot(firstThird, firstPoint);
        if (d1 <= 0.0 && d2 <= 0.0)
            return LengthSquared(firstPoint);

        const Float3 secondPoint = Subtract(point, second);
        const double d3 = Dot(firstSecond, secondPoint);
        const double d4 = Dot(firstThird, secondPoint);
        if (d3 >= 0.0 && d4 <= d3)
            return LengthSquared(secondPoint);

        const double vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
        {
            const double fraction = d1 / (d1 - d3);
            return LengthSquared(Subtract(
                point,
                Add(first, Multiply(firstSecond, fraction))));
        }

        const Float3 thirdPoint = Subtract(point, third);
        const double d5 = Dot(firstSecond, thirdPoint);
        const double d6 = Dot(firstThird, thirdPoint);
        if (d6 >= 0.0 && d5 <= d6)
            return LengthSquared(thirdPoint);

        const double vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
        {
            const double fraction = d2 / (d2 - d6);
            return LengthSquared(Subtract(
                point,
                Add(first, Multiply(firstThird, fraction))));
        }

        const double va = d3 * d6 - d5 * d4;
        if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0)
        {
            const Float3 secondThird = Subtract(third, second);
            const double fraction =
                (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return LengthSquared(Subtract(
                point,
                Add(second, Multiply(secondThird, fraction))));
        }

        const double denominator = va + vb + vc;
        if (std::abs(denominator) <= 1e-18)
        {
            return std::min({
                PointSegmentDistanceSquared(point, first, second),
                PointSegmentDistanceSquared(point, second, third),
                PointSegmentDistanceSquared(point, third, first)
            });
        }
        const double inverseDenominator = 1.0 / denominator;
        const double secondWeight = vb * inverseDenominator;
        const double thirdWeight = vc * inverseDenominator;
        const Float3 closest = Add(
            first,
            Add(
                Multiply(firstSecond, secondWeight),
                Multiply(firstThird, thirdWeight)));
        return LengthSquared(Subtract(point, closest));
    }

    double RayTriangleDistance(
        Float3 origin,
        Float3 direction,
        Float3 first,
        Float3 second,
        Float3 third)
    {
        const Float3 firstSecond = Subtract(second, first);
        const Float3 firstThird = Subtract(third, first);
        const Float3 perpendicular = Cross(direction, firstThird);
        const double determinant = Dot(firstSecond, perpendicular);
        if (std::abs(determinant) <= 1e-12)
            return std::numeric_limits<double>::infinity();

        const double inverseDeterminant = 1.0 / determinant;
        const Float3 originOffset = Subtract(origin, first);
        const double firstWeight =
            inverseDeterminant * Dot(originOffset, perpendicular);
        if (firstWeight < 0.0 || firstWeight > 1.0)
            return std::numeric_limits<double>::infinity();

        const Float3 secondPerpendicular = Cross(originOffset, firstSecond);
        const double secondWeight =
            inverseDeterminant * Dot(direction, secondPerpendicular);
        if (secondWeight < 0.0 || firstWeight + secondWeight > 1.0)
            return std::numeric_limits<double>::infinity();

        const double distance =
            inverseDeterminant * Dot(firstThird, secondPerpendicular);
        return distance > 1e-8
            ? distance
            : std::numeric_limits<double>::infinity();
    }

    const char* CgltfResultName(cgltf_result result)
    {
        switch (result)
        {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "I/O error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "legacy glTF";
        default: return "unknown result";
        }
    }

    struct CgltfDataDeleter
    {
        void operator()(cgltf_data* data) const
        {
            cgltf_free(data);
        }
    };

    using CgltfDataPtr = std::unique_ptr<cgltf_data, CgltfDataDeleter>;

    void ValidateExternalResources(
        const cgltf_data& data,
        const std::filesystem::path& sourceComponent,
        const std::filesystem::path& stagedComponent,
        const std::filesystem::path& sourceRoot,
        const std::filesystem::path& stagingRoot,
        size_t minimumExternalBufferCount)
    {
        const std::string componentContext = Generic(sourceComponent);
        if (minimumExternalBufferCount > 0u)
        {
            Require(data.file_type == cgltf_file_type_gltf,
                "downloaded scene component must be ordinary glTF: " +
                    componentContext);
            Require(data.buffers_count >= minimumExternalBufferCount,
                "downloaded scene component has fewer external buffers than "
                "its audited minimum: " + componentContext);
        }

        std::set<const cgltf_buffer*> referencedBuffers;
        for (cgltf_size viewIndex = 0u;
            viewIndex < data.buffer_views_count;
            ++viewIndex)
        {
            const cgltf_buffer_view& view = data.buffer_views[viewIndex];
            Require(view.buffer != nullptr,
                "glTF buffer view has no buffer: " + componentContext);
            referencedBuffers.insert(view.buffer);
        }

        for (cgltf_size bufferIndex = 0u;
            bufferIndex < data.buffers_count;
            ++bufferIndex)
        {
            const cgltf_buffer& buffer = data.buffers[bufferIndex];
            Require(referencedBuffers.find(&buffer) != referencedBuffers.end(),
                "glTF declares an unused buffer: " + componentContext);
            if (data.file_type == cgltf_file_type_glb && buffer.uri == nullptr)
                continue;

            Require(buffer.uri != nullptr && buffer.uri[0] != '\0',
                "glTF buffer has no external URI: " + componentContext);
            const std::string context =
                "glTF buffer " + std::to_string(bufferIndex);
            const std::filesystem::path sourceBuffer = ResolveExternalResource(
                sourceComponent,
                sourceRoot,
                buffer.uri,
                context);
            const std::filesystem::path stagedBuffer = ResolveExternalResource(
                stagedComponent,
                stagingRoot,
                buffer.uri,
                "staged " + context);
            Require(std::filesystem::file_size(sourceBuffer) == buffer.size,
                context + " byteLength differs from its file size");
            RequireFilesEqual(sourceBuffer, stagedBuffer);
        }

        for (cgltf_size imageIndex = 0u;
            imageIndex < data.images_count;
            ++imageIndex)
        {
            const cgltf_image& image = data.images[imageIndex];
            const bool hasUri = image.uri != nullptr && image.uri[0] != '\0';
            const bool hasBufferView = image.buffer_view != nullptr;
            Require(hasUri != hasBufferView,
                "glTF image must have exactly one storage source: " +
                    componentContext);
            if (hasBufferView)
            {
                Require(image.mime_type != nullptr && image.mime_type[0] != '\0',
                    "buffer-view image has no MIME type: " + componentContext);
                continue;
            }

            const std::string context =
                "glTF image " + std::to_string(imageIndex);
            const std::filesystem::path sourceImage = ResolveExternalResource(
                sourceComponent,
                sourceRoot,
                image.uri,
                context);
            const std::filesystem::path stagedImage = ResolveExternalResource(
                stagedComponent,
                stagingRoot,
                image.uri,
                "staged " + context);
            RequireFilesEqual(sourceImage, stagedImage);
        }
    }

    void ValidateRendererCompatibleMaterials(
        const cgltf_data& data,
        const std::filesystem::path& component)
    {
        const std::string context = Generic(component);
        for (cgltf_size materialIndex = 0u;
            materialIndex < data.materials_count;
            ++materialIndex)
        {
            const cgltf_material& material = data.materials[materialIndex];
            const std::string materialName =
                material.name != nullptr && material.name[0] != '\0'
                ? material.name
                : "material " + std::to_string(materialIndex);
            Require(material.alpha_mode != cgltf_alpha_mode_blend,
                "downloaded scene contains a BLEND material that UVSR would "
                    "skip ('" + materialName + "'): " + context);
            Require(!material.has_transmission,
                "downloaded scene contains a transmission material that UVSR "
                    "would skip ('" + materialName + "'): " + context);
        }
    }

    CgltfDataPtr LoadAndValidateComponent(
        const std::filesystem::path& sourceComponent,
        const std::filesystem::path& stagedComponent,
        const std::filesystem::path& sourceRoot,
        const std::filesystem::path& stagingRoot,
        size_t minimumExternalBufferCount)
    {
        RequireFilesEqual(sourceComponent, stagedComponent);

        cgltf_options options{};
        cgltf_data* rawData = nullptr;
        const std::string nativePath = sourceComponent.string();
        const cgltf_result parseResult =
            cgltf_parse_file(&options, nativePath.c_str(), &rawData);
        Require(parseResult == cgltf_result_success && rawData != nullptr,
            "cgltf failed to parse " + Generic(sourceComponent) + ": " +
                CgltfResultName(parseResult));
        CgltfDataPtr data(rawData);

        Require(data->asset.version != nullptr &&
                std::strcmp(data->asset.version, "2.0") == 0,
            "scene component is not glTF 2.0: " + Generic(sourceComponent));
        Require(data->scenes_count > 0u && data->scene != nullptr,
            "scene component has no default scene: " +
                Generic(sourceComponent));
        Require(data->nodes_count > 0u && data->meshes_count > 0u &&
                data->accessors_count > 0u &&
                data->buffer_views_count > 0u &&
                data->buffers_count > 0u,
            "scene component has no renderable glTF structure: " +
                Generic(sourceComponent));

        ValidateExternalResources(
            *data,
            sourceComponent,
            stagedComponent,
            sourceRoot,
            stagingRoot,
            minimumExternalBufferCount);
        if (minimumExternalBufferCount > 0u)
            ValidateRendererCompatibleMaterials(*data, sourceComponent);

        const cgltf_result loadResult =
            cgltf_load_buffers(&options, data.get(), nativePath.c_str());
        Require(loadResult == cgltf_result_success,
            "cgltf failed to load buffers for " + Generic(sourceComponent) +
                ": " + CgltfResultName(loadResult));
        const cgltf_result validationResult = cgltf_validate(data.get());
        Require(validationResult == cgltf_result_success,
            "cgltf rejected " + Generic(sourceComponent) + ": " +
                CgltfResultName(validationResult));

        for (cgltf_size bufferIndex = 0u;
            bufferIndex < data->buffers_count;
            ++bufferIndex)
        {
            Require(data->buffers[bufferIndex].size == 0u ||
                    data->buffers[bufferIndex].data != nullptr,
                "cgltf did not load a declared buffer for " +
                    Generic(sourceComponent));
        }
        return data;
    }

    size_t NodeIndex(
        const cgltf_data& data,
        const cgltf_node* node,
        const std::string& context)
    {
        Require(node != nullptr, context + " contains a null node");
        const ptrdiff_t index = node - data.nodes;
        Require(index >= 0 &&
                static_cast<cgltf_size>(index) < data.nodes_count,
            context + " contains a node outside the glTF node array");
        return static_cast<size_t>(index);
    }

    void VisitSceneNode(
        const cgltf_data& data,
        const cgltf_node* node,
        std::vector<unsigned char>& visitState,
        std::vector<const cgltf_node*>& sceneNodes,
        const std::string& context)
    {
        const size_t index = NodeIndex(data, node, context);
        Require(visitState[index] != 1u,
            context + " scene graph contains a cycle");
        if (visitState[index] == 2u)
            return;

        visitState[index] = 1u;
        sceneNodes.push_back(node);
        for (cgltf_size childIndex = 0u;
            childIndex < node->children_count;
            ++childIndex)
        {
            const cgltf_node* child = node->children[childIndex];
            Require(child != nullptr && child->parent == node,
                context + " scene graph has an inconsistent parent link");
            VisitSceneNode(
                data,
                child,
                visitState,
                sceneNodes,
                context);
        }
        visitState[index] = 2u;
    }

    std::vector<const cgltf_node*> CollectDefaultSceneNodes(
        const cgltf_data& data,
        const std::string& context)
    {
        Require(data.scene != nullptr && data.scene->nodes_count > 0u,
            context + " default scene has no roots");
        std::vector<unsigned char> visitState(data.nodes_count, 0u);
        std::vector<const cgltf_node*> sceneNodes;
        sceneNodes.reserve(data.nodes_count);
        for (cgltf_size rootIndex = 0u;
            rootIndex < data.scene->nodes_count;
            ++rootIndex)
        {
            const cgltf_node* root = data.scene->nodes[rootIndex];
            Require(root != nullptr && root->parent == nullptr,
                context + " default scene root has a parent");
            VisitSceneNode(
                data,
                root,
                visitState,
                sceneNodes,
                context);
        }
        Require(!sceneNodes.empty(), context + " default scene is empty");
        return sceneNodes;
    }

    const cgltf_accessor* FindPositionAccessor(
        const cgltf_primitive& primitive,
        const std::string& context)
    {
        const cgltf_accessor* position = nullptr;
        for (cgltf_size attributeIndex = 0u;
            attributeIndex < primitive.attributes_count;
            ++attributeIndex)
        {
            const cgltf_attribute& attribute =
                primitive.attributes[attributeIndex];
            if (attribute.type == cgltf_attribute_type_position &&
                attribute.index == 0)
            {
                Require(position == nullptr,
                    context + " contains duplicate POSITION attributes");
                position = attribute.data;
            }
        }
        Require(position != nullptr && position->type == cgltf_type_vec3,
            context + " has no VEC3 POSITION accessor");
        return position;
    }

    void ValidateBlenderClassroomPresentation(
        const cgltf_data& data,
        const std::string& context)
    {
        Require(data.scenes_count == 1u &&
                data.nodes_count == 876u &&
                data.meshes_count == 381u &&
                data.materials_count == 66u &&
                data.images_count == 19u &&
                data.textures_count == 36u &&
                data.samplers_count == 1u &&
                data.accessors_count == 1665u &&
                data.buffer_views_count == 1665u &&
                data.buffers_count == 1u &&
                data.cameras_count == 1u,
            context + " exported structure differs from the audited cleaned "
                "Classroom presentation");

        const std::vector<const cgltf_node*> sceneNodes =
            CollectDefaultSceneNodes(data, context);
        Require(sceneNodes.size() == 876u,
            context + " default scene must retain every audited exported node");

        constexpr std::array<const char*, 16> kRemovedTrashNodes = {{
            "Cylinder__0102",
            "Cylinder.056__0103",
            "Cylinder.057__0104",
            "Paper__0106",
            "Paper__0108",
            "Paper__0110",
            "Paper__0112",
            "Paper__0114",
            "Paper__0116",
            "Paper__0118",
            "Paper__0120",
            "Paper__0122",
            "Paper__0124",
            "Paper__0126",
            "Paper__0128",
            "Paper__0130",
        }};
        for (const char* removedNodeName : kRemovedTrashNodes)
        {
            const bool foundRemovedNode = std::any_of(
                data.nodes,
                data.nodes + data.nodes_count,
                [removedNodeName](const cgltf_node& node)
                {
                    return node.name != nullptr &&
                        std::strcmp(node.name, removedNodeName) == 0;
                });
            Require(!foundRemovedNode,
                context + " still contains removed spawn-corner trash node '" +
                    removedNodeName + "'");
        }

        const cgltf_node* retainedPaper = nullptr;
        for (cgltf_size nodeIndex = 0u;
            nodeIndex < data.nodes_count;
            ++nodeIndex)
        {
            const cgltf_node& node = data.nodes[nodeIndex];
            if (node.name == nullptr ||
                std::strcmp(node.name, "whitePages__0250") != 0)
            {
                continue;
            }
            Require(retainedPaper == nullptr,
                context + " contains duplicate unrelated whitePages paper");
            retainedPaper = &node;
        }
        Require(retainedPaper != nullptr &&
                retainedPaper->mesh != nullptr &&
                retainedPaper->mesh->primitives_count == 1u &&
                retainedPaper->mesh->primitives[0u].material != nullptr &&
                retainedPaper->mesh->primitives[0u].material->name != nullptr &&
                std::strcmp(
                    retainedPaper->mesh->primitives[0u].material->name,
                    "paper__UVSR_PBR") == 0,
            context + " must retain the unrelated renderable whitePages paper");

        constexpr std::array<const char*, 4> kRemovedTextureUris = {{
            "textures/base_bareMetal.png",
            "textures/checker.png",
            "textures/crinkledPaper.png",
            "textures/dustbin_wireframe.png",
        }};
        for (cgltf_size imageIndex = 0u;
            imageIndex < data.images_count;
            ++imageIndex)
        {
            const cgltf_image& image = data.images[imageIndex];
            for (const char* removedUri : kRemovedTextureUris)
            {
                Require(image.uri == nullptr ||
                        std::strcmp(image.uri, removedUri) != 0,
                    context + " still packages removed Classroom image '" +
                        removedUri + "'");
            }
        }

        const cgltf_material* drawingMaterial = nullptr;
        const cgltf_material* blankDrawingMaterial = nullptr;
        for (cgltf_size materialIndex = 0u;
            materialIndex < data.materials_count;
            ++materialIndex)
        {
            const cgltf_material& material = data.materials[materialIndex];
            if (material.name == nullptr)
                continue;
            if (std::strcmp(material.name, "drawing__UVSR_PBR") == 0)
            {
                Require(drawingMaterial == nullptr,
                    context + " contains duplicate drawing__UVSR_PBR materials");
                drawingMaterial = &material;
            }
            else if (std::strcmp(
                material.name,
                "drawing.004__UVSR_PBR") == 0)
            {
                Require(blankDrawingMaterial == nullptr,
                    context +
                        " contains duplicate drawing.004__UVSR_PBR materials");
                blankDrawingMaterial = &material;
            }
        }
        Require(drawingMaterial != nullptr &&
                blankDrawingMaterial != nullptr &&
                drawingMaterial != blankDrawingMaterial,
            context + " must preserve distinct checker-sheet and blank-sheet "
                "material identities");

        const cgltf_pbr_metallic_roughness& drawingPbr =
            drawingMaterial->pbr_metallic_roughness;
        const cgltf_pbr_metallic_roughness& blankDrawingPbr =
            blankDrawingMaterial->pbr_metallic_roughness;
        bool baseColorMatches = true;
        for (size_t channel = 0u; channel < 4u; ++channel)
        {
            baseColorMatches = baseColorMatches && NearlyEqual(
                drawingPbr.base_color_factor[channel],
                blankDrawingPbr.base_color_factor[channel]);
        }
        Require(drawingMaterial->has_pbr_metallic_roughness &&
                blankDrawingMaterial->has_pbr_metallic_roughness &&
                drawingMaterial->alpha_mode == cgltf_alpha_mode_opaque &&
                blankDrawingMaterial->alpha_mode == cgltf_alpha_mode_opaque &&
                drawingMaterial->double_sided &&
                blankDrawingMaterial->double_sided &&
                drawingPbr.base_color_texture.texture == nullptr &&
                blankDrawingPbr.base_color_texture.texture == nullptr &&
                drawingPbr.metallic_roughness_texture.texture == nullptr &&
                blankDrawingPbr.metallic_roughness_texture.texture == nullptr &&
                baseColorMatches &&
                NearlyEqual(drawingPbr.base_color_factor[0], 0.80000001) &&
                NearlyEqual(drawingPbr.base_color_factor[1], 0.80000001) &&
                NearlyEqual(drawingPbr.base_color_factor[2], 0.80000001) &&
                NearlyEqual(drawingPbr.base_color_factor[3], 1.0) &&
                NearlyEqual(drawingPbr.metallic_factor, 0.0) &&
                NearlyEqual(drawingPbr.roughness_factor, 1.0) &&
                NearlyEqual(
                    drawingPbr.metallic_factor,
                    blankDrawingPbr.metallic_factor) &&
                NearlyEqual(
                    drawingPbr.roughness_factor,
                    blankDrawingPbr.roughness_factor),
            context + " checker-sheet material must render identically to the "
                "audited blank-paper material");

        size_t drawingPrimitiveCount = 0u;
        size_t blankDrawingPrimitiveCount = 0u;
        for (cgltf_size meshIndex = 0u;
            meshIndex < data.meshes_count;
            ++meshIndex)
        {
            const cgltf_mesh& mesh = data.meshes[meshIndex];
            for (cgltf_size primitiveIndex = 0u;
                primitiveIndex < mesh.primitives_count;
                ++primitiveIndex)
            {
                const cgltf_material* material =
                    mesh.primitives[primitiveIndex].material;
                drawingPrimitiveCount += material == drawingMaterial ? 1u : 0u;
                blankDrawingPrimitiveCount +=
                    material == blankDrawingMaterial ? 1u : 0u;
            }
        }
        Require(drawingPrimitiveCount == 8u &&
                blankDrawingPrimitiveCount == 4u,
            context + " paper material primitive assignments differ from the "
                "audited Classroom presentation");

        uint64_t sceneTriangleCount = 0u;
        for (const cgltf_node* node : sceneNodes)
        {
            if (node->mesh == nullptr)
                continue;
            for (cgltf_size primitiveIndex = 0u;
                primitiveIndex < node->mesh->primitives_count;
                ++primitiveIndex)
            {
                const cgltf_primitive& primitive =
                    node->mesh->primitives[primitiveIndex];
                Require(primitive.type == cgltf_primitive_type_triangles,
                    context + " contains non-triangle Classroom geometry");
                const cgltf_size elementCount = primitive.indices != nullptr
                    ? primitive.indices->count
                    : FindPositionAccessor(primitive, context)->count;
                Require(elementCount % 3u == 0u,
                    context + " contains an incomplete Classroom triangle");
                sceneTriangleCount += elementCount / 3u;
            }
        }
        Require(sceneTriangleCount == 545830u,
            context + " cleaned default scene triangle count differs from the "
                "audited Classroom presentation");
    }

    Float3 ReadTransformedPosition(
        const cgltf_accessor& positions,
        cgltf_size index,
        const cgltf_float* worldTransform,
        const std::string& context)
    {
        Require(index < positions.count,
            context + " triangle index exceeds the POSITION accessor");
        std::array<cgltf_float, 3> local{};
        Require(cgltf_accessor_read_float(
                &positions,
                index,
                local.data(),
                local.size()) != 0,
            context + " failed to read a POSITION value");
        const Float3 world = TransformPoint(
            worldTransform,
            { local[0], local[1], local[2] });
        Require(IsFinite(world),
            context + " produced a non-finite transformed position");
        return world;
    }

    void ValidateEmbeddedCamera(
        const cgltf_data& data,
        const std::vector<const cgltf_node*>& sceneNodes,
        const ExpectedCamera& expected,
        const std::array<float, 3>& expectedPositionOffset,
        const std::string& context)
    {
        bool foundMatchingCamera = false;
        for (const cgltf_node* node : sceneNodes)
        {
            if (node->camera == nullptr ||
                node->camera->type != cgltf_camera_type_perspective)
            {
                continue;
            }

            std::array<cgltf_float, 16> world{};
            cgltf_node_transform_world(node, world.data());
            const Float3 position = TransformPoint(
                world.data(),
                { 0.0, 0.0, 0.0 });
            const Float3 direction = Normalize(
                TransformVector(world.data(), { 0.0, 0.0, -1.0 }),
                context + " embedded camera direction");
            const Float3 up = Normalize(
                TransformVector(world.data(), { 0.0, 1.0, 0.0 }),
                context + " embedded camera up");
            const double verticalFovDegrees =
                node->camera->data.perspective.yfov * 180.0 / kPi;

            const Float3 expectedEmbeddedPosition = Subtract(
                ToFloat3(expected.Position),
                ToFloat3(expectedPositionOffset));
            const Float3 expectedDirection = Normalize(
                ToFloat3(expected.Direction),
                context + " expected camera direction");
            const Float3 expectedUp = Normalize(
                ToFloat3(expected.Up),
                context + " expected camera up");
            const bool poseMatches =
                LengthSquared(Subtract(
                    position,
                    expectedEmbeddedPosition)) <= 1e-8 &&
                Dot(direction, expectedDirection) >= 0.999999 &&
                Dot(up, expectedUp) >= 0.999999 &&
                NearlyEqual(
                    verticalFovDegrees,
                    expected.VerticalFovDegrees,
                    1e-3);
            foundMatchingCamera = foundMatchingCamera || poseMatches;
        }
        Require(foundMatchingCamera,
            context + " descriptor camera no longer preserves its audited "
                "position offset, direction, up, and FOV relation to the "
                "embedded perspective camera");
    }

    void ValidateInitialCameraGeometry(
        const cgltf_data& data,
        const ExpectedDownloadedScene& expected,
        const std::string& context)
    {
        const std::vector<const cgltf_node*> sceneNodes =
            CollectDefaultSceneNodes(data, context);
        if (expected.CompareEmbeddedCamera)
        {
            ValidateEmbeddedCamera(
                data,
                sceneNodes,
                expected.Camera,
                expected.EmbeddedCameraPositionOffset,
                context);
        }

        const Float3 camera = ToFloat3(expected.Camera.Position);
        const Float3 forward = Normalize(
            ToFloat3(expected.Camera.Direction),
            context + " initial camera direction");
        const std::array<Float3, 6> rayDirections = {{
            { 0.0, -1.0, 0.0 },
            { 1.0, 0.0, 0.0 },
            { -1.0, 0.0, 0.0 },
            { 0.0, 0.0, 1.0 },
            { 0.0, 0.0, -1.0 },
            forward,
        }};
        std::array<double, 6> nearestRayHits;
        nearestRayHits.fill(std::numeric_limits<double>::infinity());
        double minimumDistanceSquared =
            std::numeric_limits<double>::infinity();
        Float3 boundsMinimum{
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()
        };
        Float3 boundsMaximum{
            -std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()
        };
        uint64_t triangleCount = 0u;
        bool foundTransformedMeshNode = false;

        for (const cgltf_node* node : sceneNodes)
        {
            if (node->mesh == nullptr)
                continue;

            std::array<cgltf_float, 16> worldTransform{};
            cgltf_node_transform_world(node, worldTransform.data());
            for (const cgltf_float value : worldTransform)
            {
                Require(std::isfinite(value),
                    context + " mesh node has a non-finite world transform");
            }
            foundTransformedMeshNode =
                foundTransformedMeshNode ||
                !IsIdentityMatrix(worldTransform.data());

            for (cgltf_size primitiveIndex = 0u;
                primitiveIndex < node->mesh->primitives_count;
                ++primitiveIndex)
            {
                const cgltf_primitive& primitive =
                    node->mesh->primitives[primitiveIndex];
                const std::string primitiveContext =
                    context + " primitive " +
                    std::to_string(primitiveIndex);
                Require(primitive.type == cgltf_primitive_type_triangles,
                    primitiveContext +
                        " must use independent triangle topology");
                const cgltf_accessor* positions =
                    FindPositionAccessor(primitive, primitiveContext);
                const cgltf_accessor* indices = primitive.indices;
                const cgltf_size elementCount =
                    indices != nullptr ? indices->count : positions->count;
                Require(elementCount > 0u && elementCount % 3u == 0u,
                    primitiveContext +
                        " element count must be nonzero and divisible by three");

                for (cgltf_size elementIndex = 0u;
                    elementIndex < elementCount;
                    elementIndex += 3u)
                {
                    const cgltf_size firstIndex = indices != nullptr
                        ? cgltf_accessor_read_index(indices, elementIndex)
                        : elementIndex;
                    const cgltf_size secondIndex = indices != nullptr
                        ? cgltf_accessor_read_index(indices, elementIndex + 1u)
                        : elementIndex + 1u;
                    const cgltf_size thirdIndex = indices != nullptr
                        ? cgltf_accessor_read_index(indices, elementIndex + 2u)
                        : elementIndex + 2u;
                    const Float3 first = ReadTransformedPosition(
                        *positions,
                        firstIndex,
                        worldTransform.data(),
                        primitiveContext);
                    const Float3 second = ReadTransformedPosition(
                        *positions,
                        secondIndex,
                        worldTransform.data(),
                        primitiveContext);
                    const Float3 third = ReadTransformedPosition(
                        *positions,
                        thirdIndex,
                        worldTransform.data(),
                        primitiveContext);

                    for (const Float3 vertex : { first, second, third })
                    {
                        boundsMinimum.X =
                            std::min(boundsMinimum.X, vertex.X);
                        boundsMinimum.Y =
                            std::min(boundsMinimum.Y, vertex.Y);
                        boundsMinimum.Z =
                            std::min(boundsMinimum.Z, vertex.Z);
                        boundsMaximum.X =
                            std::max(boundsMaximum.X, vertex.X);
                        boundsMaximum.Y =
                            std::max(boundsMaximum.Y, vertex.Y);
                        boundsMaximum.Z =
                            std::max(boundsMaximum.Z, vertex.Z);
                    }
                    minimumDistanceSquared = std::min(
                        minimumDistanceSquared,
                        PointTriangleDistanceSquared(
                            camera,
                            first,
                            second,
                            third));
                    for (size_t rayIndex = 0u;
                        rayIndex < rayDirections.size();
                        ++rayIndex)
                    {
                        nearestRayHits[rayIndex] = std::min(
                            nearestRayHits[rayIndex],
                            RayTriangleDistance(
                                camera,
                                rayDirections[rayIndex],
                                first,
                                second,
                                third));
                    }
                    ++triangleCount;
                }
            }
        }

        Require(triangleCount > 0u &&
                std::isfinite(minimumDistanceSquared),
            context + " camera geometry audit found no triangles");
        Require(!expected.ExpectTransformedMeshNodes ||
                foundTransformedMeshNode,
            context + " no longer exercises transformed mesh nodes");
        Require(camera.X > boundsMinimum.X &&
                camera.X < boundsMaximum.X &&
                camera.Y > boundsMinimum.Y &&
                camera.Y < boundsMaximum.Y &&
                camera.Z > boundsMinimum.Z &&
                camera.Z < boundsMaximum.Z,
            context + " initial camera is outside the scene bounds");

        const double sceneDiagonal = std::sqrt(
            LengthSquared(Subtract(boundsMaximum, boundsMinimum)));
        Require(std::isfinite(sceneDiagonal) && sceneDiagonal > 0.0,
            context + " scene bounds are invalid");
        const double collisionRadius = std::max(
            0.1,
            std::max(sceneDiagonal, 100.0) * 0.0005);
        const double minimumClearance =
            std::sqrt(minimumDistanceSquared);
        std::cout << context << " camera audit candidate: clearance="
                  << minimumClearance << ", collisionRadius="
                  << collisionRadius << ", diagonal=" << sceneDiagonal
                  << ", rays[down,+X,-X,+Z,-Z,forward]=["
                  << nearestRayHits[0] << ',' << nearestRayHits[1] << ','
                  << nearestRayHits[2] << ',' << nearestRayHits[3] << ','
                  << nearestRayHits[4] << ',' << nearestRayHits[5] << ']'
                  << std::endl;
        Require(minimumClearance >= collisionRadius + 0.01,
            context +
                " initial camera intersects its collision sphere with scene geometry");
        Require(std::isfinite(nearestRayHits[0]) &&
                nearestRayHits[0] > collisionRadius &&
                nearestRayHits[0] <= 3.0,
            context +
                " initial camera must have a reachable floor within three meters");
        size_t horizontalEnclosureRayCount = 0u;
        for (size_t rayIndex = 1u; rayIndex <= 4u; ++rayIndex)
        {
            if (!std::isfinite(nearestRayHits[rayIndex]))
                continue;
            Require(nearestRayHits[rayIndex] > collisionRadius &&
                    nearestRayHits[rayIndex] <= sceneDiagonal * 1.01,
                context +
                    " initial camera has an invalid horizontal enclosure hit "
                    "at ray index " +
                    std::to_string(rayIndex) + " with distance " +
                    std::to_string(nearestRayHits[rayIndex]));
            ++horizontalEnclosureRayCount;
        }
        Require(horizontalEnclosureRayCount >=
                expected.MinimumHorizontalEnclosureRayCount,
            context + " initial camera has only " +
                std::to_string(horizontalEnclosureRayCount) +
                " horizontal enclosure hits; expected at least " +
                std::to_string(expected.MinimumHorizontalEnclosureRayCount));
        Require(std::isfinite(nearestRayHits[5]) &&
                nearestRayHits[5] > collisionRadius &&
                nearestRayHits[5] <= sceneDiagonal * 1.01,
            context +
                " initial camera must face reachable scene geometry");

        std::cout << context << " initial camera: "
                  << minimumClearance << " m geometry clearance, "
                  << nearestRayHits[0] << " m above floor, "
                  << triangleCount << " transformed triangles audited\n";
    }

    const ExpectedDownloadedScene* FindDownloadedScene(
        std::string_view descriptorRelativePath)
    {
        const auto match = std::find_if(
            kDownloadedScenes.begin(),
            kDownloadedScenes.end(),
            [descriptorRelativePath](const ExpectedDownloadedScene& scene)
            {
                return descriptorRelativePath ==
                    scene.DescriptorRelativePath;
            });
        return match == kDownloadedScenes.end() ? nullptr : &*match;
    }

    std::vector<std::string> DiscoverSceneFiles(
        const std::filesystem::path& root)
    {
        std::vector<std::string> discovered;
        for (const auto& item :
            std::filesystem::recursive_directory_iterator(root))
        {
            if (item.is_regular_file() && IsLoadableSceneFile(item.path()))
                discovered.push_back(Generic(item.path()));
        }
        return discovered;
    }

    std::vector<uvsr::SceneCatalogEntry> ValidateStagedCatalog(
        donut::vfs::IFileSystem& fileSystem,
        const std::filesystem::path& stagingRoot)
    {
        const std::vector<std::string> discovered =
            DiscoverSceneFiles(stagingRoot);
        const std::vector<uvsr::SceneCatalogEntry> catalog =
            uvsr::BuildSceneCatalog(fileSystem, stagingRoot, discovered);

        std::set<std::string> actualDisplayNames;
        std::set<std::string> actualRelativePaths;
        for (const uvsr::SceneCatalogEntry& entry : catalog)
        {
            actualDisplayNames.insert(entry.DisplayName);
            const std::filesystem::path relative =
                std::filesystem::path(entry.FileName)
                    .lexically_normal()
                    .lexically_relative(stagingRoot.lexically_normal());
            Require(!relative.empty() && !relative.is_absolute(),
                "catalog entry lies outside the staged scene root: " +
                    entry.FileName);
            actualRelativePaths.insert(relative.generic_string());
        }

        std::set<std::string> expectedDisplayNames;
        std::set<std::string> expectedRelativePaths;
        for (const ExpectedDescriptor& expected : kExpectedDescriptors)
        {
            expectedDisplayNames.insert(expected.DisplayName);
            expectedRelativePaths.insert(expected.RelativePath);
        }
        Require(catalog.size() == kExpectedDescriptors.size() &&
                actualDisplayNames == expectedDisplayNames &&
                actualRelativePaths == expectedRelativePaths,
            "staged scene catalog must contain exactly Sponza Decorated, "
                "Sponza Plain, Bistro Interior, San Miguel, and Classroom "
                "Interior");
        return catalog;
    }

    void ValidateDescriptorCamera(
        const Json::Value& descriptor,
        const ExpectedDownloadedScene& expected,
        const uvsr::SceneCatalogEntry& catalogEntry,
        const std::string& context)
    {
        const Json::Value& camera = descriptor["initialCamera"];
        Require(camera.isObject(),
            context + " descriptor has no initialCamera object");
        const std::array<float, 3> position =
            ReadFloat3(camera["position"], context + " camera position");
        const std::array<float, 3> direction =
            ReadFloat3(camera["direction"], context + " camera direction");
        const std::array<float, 3> up =
            ReadFloat3(camera["up"], context + " camera up");
        Require(NearlyEqual(position, expected.Camera.Position) &&
                NearlyEqual(direction, expected.Camera.Direction) &&
                NearlyEqual(up, expected.Camera.Up),
            context + " descriptor camera pose differs from the audited pose");

        const Float3 normalizedDirection = Normalize(
            ToFloat3(direction),
            context + " camera direction");
        const Float3 normalizedUp = Normalize(
            ToFloat3(up),
            context + " camera up");
        Require(std::abs(Dot(normalizedDirection, normalizedUp)) <= 1e-5,
            context + " camera direction and up must be orthogonal");
        Require(camera["verticalFovDegrees"].isNumeric() &&
                std::isfinite(camera["verticalFovDegrees"].asFloat()) &&
                NearlyEqual(
                    camera["verticalFovDegrees"].asFloat(),
                    expected.Camera.VerticalFovDegrees),
            context + " descriptor camera FOV differs from the audited FOV");

        Require(catalogEntry.InitialCamera.has_value(),
            context + " scene catalog discarded the valid initial camera");
        const uvsr::SceneInitialCamera& catalogCamera =
            *catalogEntry.InitialCamera;
        Require(NearlyEqual(
                    catalogCamera.Position,
                    expected.Camera.Position) &&
                NearlyEqual(
                    catalogCamera.Direction,
                    expected.Camera.Direction) &&
                NearlyEqual(
                    catalogCamera.Up,
                    expected.Camera.Up) &&
                NearlyEqual(
                    catalogCamera.VerticalFovDegrees,
                    expected.Camera.VerticalFovDegrees),
            context + " catalog camera differs from descriptor metadata");
    }

    std::filesystem::path ResolveDescriptorModel(
        const std::filesystem::path& descriptor,
        const std::filesystem::path& root,
        const std::string& model,
        const std::string& context)
    {
        const std::filesystem::path relativeModel(model);
        Require(!relativeModel.empty() && !relativeModel.is_absolute() &&
                !relativeModel.has_root_name() &&
                !relativeModel.has_root_directory(),
            context + " model reference must be relative");
        const std::filesystem::path component =
            (descriptor.parent_path() / relativeModel).lexically_normal();
        Require(IsContainedBy(root, component),
            context + " model reference escapes the supported scene root");
        RequireRegularFileBelowLimit(component, context + " model");
        return component;
    }

    void ValidateDescriptor(
        donut::vfs::IFileSystem& fileSystem,
        const std::filesystem::path& sourceRoot,
        const std::filesystem::path& stagingRoot,
        const ExpectedDescriptor& expected,
        const std::vector<uvsr::SceneCatalogEntry>& catalog,
        std::set<std::string>& validatedComponents)
    {
        const std::filesystem::path sourceDescriptor =
            (sourceRoot / expected.RelativePath).lexically_normal();
        const std::filesystem::path stagedDescriptor =
            (stagingRoot / expected.RelativePath).lexically_normal();
        RequireFilesEqual(sourceDescriptor, stagedDescriptor);

        Json::Value descriptor;
        Require(donut::json::LoadFromFile(
                fileSystem,
                Generic(sourceDescriptor),
                descriptor) && descriptor.isObject(),
            "failed to parse scene descriptor: " + Generic(sourceDescriptor));
        Require(descriptor["displayName"].isString() &&
                descriptor["displayName"].asString() == expected.DisplayName,
            "scene descriptor displayName differs: " +
                Generic(sourceDescriptor));
        Require(descriptor["models"].isArray() &&
                descriptor["models"].size() == expected.ModelCount,
            "scene descriptor model count differs: " +
                Generic(sourceDescriptor));
        Require(descriptor["graph"].isArray() &&
                descriptor["graph"].size() == descriptor["models"].size(),
            "scene descriptor graph must instantiate every model exactly "
                "once: " + Generic(sourceDescriptor));

        std::vector<bool> instantiatedModels(expected.ModelCount, false);
        for (const Json::Value& graphNode : descriptor["graph"])
        {
            Require(graphNode.isObject() && graphNode["model"].isUInt(),
                "scene descriptor graph has an invalid model index: " +
                    Generic(sourceDescriptor));
            const Json::ArrayIndex modelIndex =
                graphNode["model"].asUInt();
            Require(modelIndex < expected.ModelCount &&
                    !instantiatedModels[modelIndex],
                "scene descriptor graph repeats or exceeds a model index: " +
                    Generic(sourceDescriptor));
            instantiatedModels[modelIndex] = true;
        }
        Require(std::all_of(
                instantiatedModels.begin(),
                instantiatedModels.end(),
                [](bool instantiated) { return instantiated; }),
            "scene descriptor graph omits a model: " +
                Generic(sourceDescriptor));

        const uvsr::SceneCatalogEntry* catalogEntry =
            uvsr::FindSceneCatalogEntry(catalog, Generic(stagedDescriptor));
        Require(catalogEntry != nullptr &&
                catalogEntry->DisplayName == expected.DisplayName,
            "scene descriptor is missing or mislabeled in the staged catalog: " +
                Generic(stagedDescriptor));

        const ExpectedDownloadedScene* downloaded =
            FindDownloadedScene(expected.RelativePath);
        if (downloaded != nullptr)
        {
            ValidateDescriptorCamera(
                descriptor,
                *downloaded,
                *catalogEntry,
                expected.DisplayName);
            Require(descriptor["models"][0u].isString() &&
                    descriptor["models"][0u].asString() ==
                        downloaded->ModelRelativePath,
                std::string(expected.DisplayName) +
                    " descriptor model path differs from the runtime contract");
        }

        for (Json::ArrayIndex modelIndex = 0u;
            modelIndex < descriptor["models"].size();
            ++modelIndex)
        {
            const Json::Value& modelValue = descriptor["models"][modelIndex];
            Require(modelValue.isString() && !modelValue.asString().empty(),
                "scene descriptor contains an invalid model reference: " +
                    Generic(sourceDescriptor));
            const std::filesystem::path sourceComponent =
                ResolveDescriptorModel(
                    sourceDescriptor,
                    sourceRoot,
                    modelValue.asString(),
                    expected.DisplayName);
            const std::filesystem::path relativeComponent =
                sourceComponent.lexically_relative(sourceRoot);
            const std::filesystem::path stagedComponent =
                (stagingRoot / relativeComponent).lexically_normal();
            RequireRegularFileBelowLimit(
                stagedComponent,
                std::string(expected.DisplayName) + " staged model");
            Require(uvsr::FindSceneCatalogEntry(
                    catalog,
                    Generic(stagedComponent)) == nullptr,
                "descriptor-owned model is visible in the staged scene picker: " +
                    Generic(stagedComponent));

            const std::string componentKey = Generic(sourceComponent);
            if (!validatedComponents.insert(componentKey).second)
                continue;

            const bool isDownloadedComponent = downloaded != nullptr;
            if (isDownloadedComponent)
            {
                Require(EndsWithCaseInsensitive(componentKey, ".gltf"),
                    "downloaded scene component must use standard .gltf");
            }
            else
            {
                Require(EndsWithCaseInsensitive(componentKey, ".glb"),
                    "Intel PBR Sponza components must remain GLB files");
            }

            // Keep the cgltf object local to this iteration. Downloaded scenes
            // can carry large external buffers, so retaining one scene while
            // opening the next would needlessly increase peak memory in CI.
            const size_t minimumExternalBufferCount = downloaded != nullptr
                ? downloaded->MinimumExternalBufferCount
                : 0u;
            CgltfDataPtr component = LoadAndValidateComponent(
                sourceComponent,
                stagedComponent,
                sourceRoot,
                stagingRoot,
                minimumExternalBufferCount);
            if (downloaded != nullptr)
            {
                if (std::string_view(expected.RelativePath) ==
                    "blender_classroom/blender_classroom.scene.json")
                {
                    ValidateBlenderClassroomPresentation(
                        *component,
                        expected.DisplayName);
                }
                ValidateInitialCameraGeometry(
                    *component,
                    *downloaded,
                    expected.DisplayName);
            }
        }
    }
}

int main(int argumentCount, char** arguments)
{
    try
    {
        Require(argumentCount == 3,
            "usage: uvsr_scene_asset_contract_tests "
            "<source-scene-root> <staged-scene-root>");
        const std::filesystem::path sourceRoot =
            std::filesystem::absolute(arguments[1]).lexically_normal();
        const std::filesystem::path stagingRoot =
            std::filesystem::absolute(arguments[2]).lexically_normal();
        Require(std::filesystem::is_directory(sourceRoot),
            "source scene root is missing: " + Generic(sourceRoot));
        Require(std::filesystem::is_directory(stagingRoot),
            "staged scene root is missing: " + Generic(stagingRoot));

        for (const char* sceneDirectory : kSupportedSceneDirectories)
        {
            ValidateStagedSceneTree(
                sourceRoot,
                stagingRoot,
                sceneDirectory);
        }
        Require(!std::filesystem::exists(stagingRoot / "nvidia_bistro"),
            "the preserved source-only NVIDIA Bistro downloads must not be "
                "staged as standalone runtime scenes");

        donut::vfs::NativeFileSystem fileSystem;
        const std::vector<uvsr::SceneCatalogEntry> catalog =
            ValidateStagedCatalog(fileSystem, stagingRoot);

        std::set<std::string> validatedComponents;
        for (const ExpectedDescriptor& expected : kExpectedDescriptors)
        {
            ValidateDescriptor(
                fileSystem,
                sourceRoot,
                stagingRoot,
                expected,
                catalog,
                validatedComponents);
        }

        std::cout << "scene asset contract tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "scene asset contract tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
