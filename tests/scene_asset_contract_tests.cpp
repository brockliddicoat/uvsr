#include "scene_catalog.h"

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
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr uint64_t kMaximumGlbBytes = 100'000'000ull;
    constexpr uint32_t kGlbVersion = 2;
    constexpr uint32_t kJsonChunkType = 0x4e4f534a;
    constexpr uint32_t kBinaryChunkType = 0x004e4942;

    struct Float3
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
    };

    struct GlbPayload
    {
        Json::Value Document;
        std::vector<unsigned char> Binary;
    };

    struct AccessorView
    {
        const std::vector<unsigned char>* Binary = nullptr;
        uint64_t Offset = 0;
        uint64_t Stride = 0;
        uint64_t Count = 0;
        uint32_t ComponentType = 0;
        uint32_t ComponentBytes = 0;
        uint32_t Components = 0;
    };

    struct ExpectedScene
    {
        const char* Descriptor;
        const char* DisplayName;
        std::array<float, 3> Position;
        std::array<float, 3> Direction;
    };

    constexpr std::array<ExpectedScene, 2> kExpectedScenes = {{
        {
            "living_room/living_room.scene.json",
            "Living Room",
            { 0.f, 1.65f, 6.25f },
            { 0.f, -0.079984f, -0.996796f }
        },
        {
            "sponza/sponza.scene.json",
            "Sponza",
            { 0.f, 1.6f, 0.f },
            { 1.f, 0.f, 0.f }
        },
    }};
    constexpr std::array<const char*, 2> kRemovedSceneSlugs = {{
        "salle_de_bain",
        "sibenik",
    }};
    constexpr std::array<const char*, 4> kExpectedCatalogDisplayNames = {{
        "Living Room",
        "PBR Sponza Decorated",
        "PBR Sponza Plain",
        "Sponza",
    }};

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool TryAddUint64(uint64_t left, uint64_t right, uint64_t& result)
    {
        if (right > std::numeric_limits<uint64_t>::max() - left)
            return false;
        result = left + right;
        return true;
    }

    bool TryMultiplyUint64(uint64_t left, uint64_t right, uint64_t& result)
    {
        if (left != 0u &&
            right > std::numeric_limits<uint64_t>::max() / left)
        {
            return false;
        }
        result = left * right;
        return true;
    }

    bool TryGetAccessorByteRange(
        uint64_t bufferViewOffset,
        uint64_t bufferViewLength,
        uint64_t accessorOffset,
        uint64_t count,
        uint64_t stride,
        uint64_t elementBytes,
        uint64_t binaryBytes,
        uint64_t& absoluteOffset,
        uint64_t& rangeEnd)
    {
        uint64_t bufferViewEnd = 0u;
        if (!TryAddUint64(
                bufferViewOffset,
                bufferViewLength,
                bufferViewEnd) ||
            bufferViewEnd > binaryBytes ||
            accessorOffset > bufferViewLength ||
            elementBytes == 0u ||
            stride < elementBytes ||
            !TryAddUint64(
                bufferViewOffset,
                accessorOffset,
                absoluteOffset))
        {
            return false;
        }

        uint64_t relativeEnd = accessorOffset;
        if (count != 0u)
        {
            uint64_t stridedBytes = 0u;
            if (!TryMultiplyUint64(count - 1u, stride, stridedBytes) ||
                !TryAddUint64(relativeEnd, stridedBytes, relativeEnd) ||
                !TryAddUint64(relativeEnd, elementBytes, relativeEnd))
            {
                return false;
            }
        }

        return relativeEnd <= bufferViewLength &&
            TryAddUint64(bufferViewOffset, relativeEnd, rangeEnd) &&
            rangeEnd <= bufferViewEnd &&
            rangeEnd <= binaryBytes;
    }

    void ValidateAccessorByteRangeArithmetic()
    {
        uint64_t offset = 0u;
        uint64_t end = 0u;
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();

        Require(
            TryGetAccessorByteRange(
                16u, 64u, 8u, 3u, 12u, 12u, 128u, offset, end) &&
                offset == 24u &&
                end == 60u,
            "checked accessor range rejected a valid strided range");
        Require(
            TryGetAccessorByteRange(
                16u, 64u, 64u, 0u, 12u, 12u, 128u, offset, end) &&
                offset == 80u &&
                end == 80u,
            "checked accessor range rejected a valid empty range");
        Require(
            !TryGetAccessorByteRange(
                maximum - 3u,
                4u,
                0u,
                1u,
                1u,
                1u,
                maximum,
                offset,
                end),
            "checked accessor range accepted an overflowing buffer view");
        Require(
            !TryGetAccessorByteRange(
                0u,
                maximum,
                0u,
                3u,
                maximum / 2u + 1u,
                1u,
                maximum,
                offset,
                end),
            "checked accessor range accepted an overflowing stride product");
        Require(
            !TryGetAccessorByteRange(
                0u,
                maximum,
                0u,
                2u,
                maximum - 1u,
                2u,
                maximum,
                offset,
                end),
            "checked accessor range accepted an overflowing range end");
        Require(
            !TryGetAccessorByteRange(
                16u, 32u, 24u, 2u, 8u, 8u, 128u, offset, end),
            "checked accessor range crossed its declared buffer view");
        Require(
            !TryGetAccessorByteRange(
                64u, 64u, 0u, 1u, 8u, 8u, 100u, offset, end),
            "checked accessor range crossed the embedded binary buffer");
    }

    uint32_t ReadUint32(std::istream& stream, const std::string& context)
    {
        std::array<unsigned char, 4> bytes = {};
        stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        Require(stream.good(), "failed to read " + context);
        return static_cast<uint32_t>(bytes[0])
            | (static_cast<uint32_t>(bytes[1]) << 8)
            | (static_cast<uint32_t>(bytes[2]) << 16)
            | (static_cast<uint32_t>(bytes[3]) << 24);
    }

    bool EndsWithCaseInsensitive(std::string value, std::string suffix)
    {
        if (suffix.size() > value.size())
            return false;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool IsLoadableSceneFile(const std::filesystem::path& path)
    {
        const std::string name = path.generic_string();
        return EndsWithCaseInsensitive(name, ".scene.json")
            || EndsWithCaseInsensitive(name, ".gltf")
            || EndsWithCaseInsensitive(name, ".glb");
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

    bool NearlyEqual(float left, float right)
    {
        return std::abs(left - right) <= 1e-5f;
    }

    bool NearlyEqual(
        const std::array<float, 3>& left,
        const std::array<float, 3>& right)
    {
        return NearlyEqual(left[0], right[0]) &&
            NearlyEqual(left[1], right[1]) &&
            NearlyEqual(left[2], right[2]);
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

    Float3 Normalize(Float3 value)
    {
        const double lengthSquared = LengthSquared(value);
        Require(lengthSquared > 1e-12 && std::isfinite(lengthSquared),
            "camera direction must be finite and nonzero");
        return Multiply(value, 1.0 / std::sqrt(lengthSquared));
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

        const Float3 secondPerpendicular =
            Cross(originOffset, firstSecond);
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

    bool IsContainedBy(
        const std::filesystem::path& root,
        const std::filesystem::path& candidate)
    {
        const std::filesystem::path relative =
            candidate.lexically_normal().lexically_relative(root.lexically_normal());
        if (relative.empty() || relative.is_absolute())
            return false;
        const auto first = relative.begin();
        return first == relative.end() || *first != "..";
    }

    void RequireFilesEqual(
        const std::filesystem::path& source,
        const std::filesystem::path& staged)
    {
        Require(std::filesystem::is_regular_file(staged),
            "missing staged scene asset: " + staged.generic_string());
        Require(std::filesystem::file_size(source) == std::filesystem::file_size(staged),
            "staged scene asset size differs: " + staged.generic_string());

        std::ifstream left(source, std::ios::binary);
        std::ifstream right(staged, std::ios::binary);
        Require(left.good() && right.good(), "failed to open staged comparison inputs");
        std::array<char, 64 * 1024> leftBytes = {};
        std::array<char, 64 * 1024> rightBytes = {};
        while (left.good() || right.good())
        {
            left.read(leftBytes.data(), leftBytes.size());
            right.read(rightBytes.data(), rightBytes.size());
            Require(left.gcount() == right.gcount(),
                "staged scene asset byte count differs: " + staged.generic_string());
            Require(std::equal(
                leftBytes.begin(),
                leftBytes.begin() + left.gcount(),
                rightBytes.begin()),
                "staged scene asset bytes differ: " + staged.generic_string());
        }
    }

    GlbPayload LoadGlb(const std::filesystem::path& path)
    {
        const uint64_t fileSize = std::filesystem::file_size(path);
        Require(fileSize < kMaximumGlbBytes,
            "GLB reaches or exceeds 100 MB: " + path.generic_string());
        Require(fileSize >= 28u,
            "GLB is too small to contain JSON and binary chunks: "
                + path.generic_string());

        std::ifstream stream(path, std::ios::binary);
        Require(stream.good(), "failed to open GLB: " + path.generic_string());

        std::array<char, 4> magic = {};
        stream.read(magic.data(), magic.size());
        Require(magic == std::array<char, 4>{ 'g', 'l', 'T', 'F' },
            "invalid GLB magic: " + path.generic_string());
        Require(ReadUint32(stream, "GLB version") == kGlbVersion,
            "invalid GLB version: " + path.generic_string());
        Require(ReadUint32(stream, "GLB declared length") == fileSize,
            "invalid GLB declared length: " + path.generic_string());

        const uint32_t jsonLength = ReadUint32(stream, "GLB JSON length");
        Require(ReadUint32(stream, "GLB JSON type") == kJsonChunkType,
            "GLB does not begin with JSON: " + path.generic_string());
        Require(jsonLength <= fileSize - 20,
            "GLB JSON chunk exceeds file bounds: " + path.generic_string());

        std::string json(jsonLength, '\0');
        stream.read(json.data(), json.size());
        Require(stream.good(), "failed to read GLB JSON: " + path.generic_string());

        Json::Value document;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream jsonStream(json);
        Require(Json::parseFromStream(builder, jsonStream, &document, &errors),
            "failed to parse GLB JSON for " + path.generic_string() + ": " + errors);

        const uint32_t binaryLength =
            ReadUint32(stream, "GLB binary length");
        Require(ReadUint32(stream, "GLB binary type") == kBinaryChunkType,
            "GLB JSON is not followed by a binary chunk: "
                + path.generic_string());
        const uint64_t payloadBytes =
            28ull + uint64_t(jsonLength) + uint64_t(binaryLength);
        Require(payloadBytes == fileSize,
            "GLB chunks do not exactly cover the declared file: "
                + path.generic_string());

        std::vector<unsigned char> binary(binaryLength);
        stream.read(
            reinterpret_cast<char*>(binary.data()),
            std::streamsize(binary.size()));
        Require(stream.good(),
            "failed to read GLB binary: " + path.generic_string());
        return { std::move(document), std::move(binary) };
    }

    GlbPayload ValidateGlb(const std::filesystem::path& path)
    {
        GlbPayload payload = LoadGlb(path);
        const Json::Value& document = payload.Document;
        Require(document["asset"]["version"].asString() == "2.0",
            "GLB asset is not glTF 2.0: " + path.generic_string());
        Require(document["scenes"].isArray() && !document["scenes"].empty(),
            "GLB has no scenes: " + path.generic_string());
        Require(document["meshes"].isArray() && !document["meshes"].empty(),
            "GLB has no meshes: " + path.generic_string());
        Require(document["materials"].isArray() && !document["materials"].empty(),
            "GLB has no materials: " + path.generic_string());
        Require(document["images"].isArray() && !document["images"].empty(),
            "GLB has no embedded images: " + path.generic_string());
        Require(document["buffers"].isArray() && !document["buffers"].empty(),
            "GLB has no buffers: " + path.generic_string());
        Require(!document.isMember("extensionsRequired")
                || document["extensionsRequired"].empty(),
            "GLB requires extensions: " + path.generic_string());
        Require(document["buffers"].size() == 1u,
            "GLB must contain exactly one embedded buffer: "
                + path.generic_string());

        const Json::Value& accessors = document["accessors"];
        const Json::Value& materials = document["materials"];
        for (const Json::Value& buffer : document["buffers"])
        {
            Require(!buffer.isMember("uri"),
                "GLB buffer is not self-contained: " + path.generic_string());
            Require(buffer["byteLength"].isUInt64()
                    && buffer["byteLength"].asUInt64() <= payload.Binary.size(),
                "GLB embedded buffer length exceeds its binary chunk: "
                    + path.generic_string());
        }
        for (const Json::Value& image : document["images"])
        {
            Require(image.isMember("bufferView") && !image.isMember("uri"),
                "GLB image is not embedded: " + path.generic_string());
        }
        for (const Json::Value& material : materials)
        {
            Require(material.get("alphaMode", "OPAQUE").asString() != "BLEND",
                "GLB contains unsupported blended material: " + path.generic_string());
            Require(!material.isMember("normalTexture"),
                "GLB exposes an OBJ height map as a normal map: " + path.generic_string());
            Require(!material["extensions"].isMember("KHR_materials_transmission"),
                "GLB contains unsupported transmissive material: " + path.generic_string());
        }
        for (const Json::Value& mesh : document["meshes"])
        {
            for (const Json::Value& primitive : mesh["primitives"])
            {
                const Json::Value& attributes = primitive["attributes"];
                Require(attributes.isMember("POSITION"),
                    "GLB primitive has no positions: " + path.generic_string());
                Require(attributes.isMember("NORMAL"),
                    "GLB primitive has no normals: " + path.generic_string());
                for (const std::string& attribute : attributes.getMemberNames())
                {
                    const Json::ArrayIndex accessor = attributes[attribute].asUInt();
                    Require(accessor < accessors.size(),
                        "GLB primitive has an invalid accessor: " + path.generic_string());
                }
                if (primitive.isMember("indices"))
                {
                    Require(primitive["indices"].asUInt() < accessors.size(),
                        "GLB primitive has invalid indices: " + path.generic_string());
                }
                if (primitive.isMember("material"))
                {
                    Require(primitive["material"].asUInt() < materials.size(),
                        "GLB primitive has an invalid material: " + path.generic_string());
                }
            }
        }
        return payload;
    }

    uint32_t GetComponentBytes(
        uint32_t componentType,
        const std::string& context)
    {
        switch (componentType)
        {
        case 5121u: return 1u;
        case 5123u: return 2u;
        case 5125u:
        case 5126u:
            return 4u;
        default:
            Require(false, context + " uses an unsupported component type");
            return 0u;
        }
    }

    uint32_t GetComponentCount(
        const std::string& accessorType,
        const std::string& context)
    {
        if (accessorType == "SCALAR")
            return 1u;
        if (accessorType == "VEC2")
            return 2u;
        if (accessorType == "VEC3")
            return 3u;
        if (accessorType == "VEC4")
            return 4u;
        Require(false, context + " uses an unsupported accessor type");
        return 0u;
    }

    AccessorView GetAccessorView(
        const GlbPayload& payload,
        Json::ArrayIndex accessorIndex,
        const std::string& context)
    {
        const Json::Value& accessors = payload.Document["accessors"];
        Require(accessorIndex < accessors.size(),
            context + " has an invalid accessor index");
        const Json::Value& accessor = accessors[accessorIndex];
        Require(accessor.isObject() && accessor["bufferView"].isUInt(),
            context + " must use a buffer view");
        Require(!accessor.isMember("sparse"),
            context + " must not use a sparse accessor");

        const Json::Value& bufferViews = payload.Document["bufferViews"];
        const Json::ArrayIndex bufferViewIndex =
            accessor["bufferView"].asUInt();
        Require(bufferViewIndex < bufferViews.size(),
            context + " has an invalid buffer-view index");
        const Json::Value& bufferView = bufferViews[bufferViewIndex];
        Require(bufferView.isObject() &&
                bufferView.get("buffer", 0u).isUInt() &&
                bufferView.get("buffer", 0u).asUInt() == 0u,
            context + " must reference the embedded GLB buffer");
        Require(bufferView["byteLength"].isUInt64(),
            context + " buffer view has an invalid byte length");
        Require(!bufferView.isMember("byteOffset") ||
                bufferView["byteOffset"].isUInt64(),
            context + " buffer view has an invalid byte offset");
        Require(!bufferView.isMember("byteStride") ||
                bufferView["byteStride"].isUInt64(),
            context + " buffer view has an invalid byte stride");
        Require(!accessor.isMember("byteOffset") ||
                accessor["byteOffset"].isUInt64(),
            context + " has an invalid byte offset");
        Require(accessor["count"].isUInt64(),
            context + " has an invalid element count");
        Require(accessor["componentType"].isUInt() &&
                accessor["type"].isString(),
            context + " has an invalid element format");

        const uint32_t componentType =
            accessor["componentType"].asUInt();
        const uint32_t componentBytes =
            GetComponentBytes(componentType, context);
        const uint32_t components =
            GetComponentCount(accessor["type"].asString(), context);
        uint64_t elementBytes = 0u;
        Require(
            TryMultiplyUint64(componentBytes, components, elementBytes) &&
                elementBytes != 0u,
            context + " element byte size overflows");
        const uint64_t stride =
            bufferView.isMember("byteStride")
                ? bufferView["byteStride"].asUInt64()
                : elementBytes;
        Require(stride >= elementBytes,
            context + " has a byte stride smaller than one element");

        const uint64_t bufferViewOffset =
            bufferView.get("byteOffset", 0u).asUInt64();
        const uint64_t bufferViewLength =
            bufferView["byteLength"].asUInt64();
        const uint64_t accessorOffset =
            accessor.get("byteOffset", 0u).asUInt64();
        const uint64_t count = accessor["count"].asUInt64();
        uint64_t offset = 0u;
        uint64_t rangeEnd = 0u;
        Require(
            TryGetAccessorByteRange(
                bufferViewOffset,
                bufferViewLength,
                accessorOffset,
                count,
                stride,
                elementBytes,
                static_cast<uint64_t>(payload.Binary.size()),
                offset,
                rangeEnd),
            context +
                " byte range overflows or exceeds its buffer view or embedded GLB buffer");

        return {
            &payload.Binary,
            offset,
            stride,
            count,
            componentType,
            componentBytes,
            components
        };
    }

    template <typename Value>
    Value ReadBinaryValue(
        const AccessorView& accessor,
        uint64_t element,
        uint32_t component)
    {
        Require(element < accessor.Count &&
                component < accessor.Components,
            "GLB accessor read exceeds its declared range");
        Require(accessor.Binary != nullptr,
            "GLB accessor read has no embedded buffer");
        Value result{};
        uint64_t elementOffset = 0u;
        uint64_t componentOffset = 0u;
        uint64_t offset = 0u;
        uint64_t end = 0u;
        Require(
            TryMultiplyUint64(element, accessor.Stride, elementOffset) &&
                TryMultiplyUint64(
                    component,
                    accessor.ComponentBytes,
                    componentOffset) &&
                TryAddUint64(accessor.Offset, elementOffset, offset) &&
                TryAddUint64(offset, componentOffset, offset) &&
                TryAddUint64(offset, sizeof(Value), end) &&
                end <= accessor.Binary->size(),
            "GLB accessor read byte range overflows or exceeds the embedded buffer");
        std::memcpy(
            &result,
            accessor.Binary->data() + offset,
            sizeof(Value));
        return result;
    }

    Float3 ReadPosition(
        const AccessorView& accessor,
        uint64_t element,
        const std::string& context)
    {
        Require(accessor.ComponentType == 5126u &&
                accessor.ComponentBytes == sizeof(float) &&
                accessor.Components == 3u,
            context + " position accessor must be float32 VEC3");
        return {
            ReadBinaryValue<float>(accessor, element, 0u),
            ReadBinaryValue<float>(accessor, element, 1u),
            ReadBinaryValue<float>(accessor, element, 2u)
        };
    }

    uint64_t ReadIndex(
        const AccessorView& accessor,
        uint64_t element,
        const std::string& context)
    {
        Require(accessor.Components == 1u,
            context + " index accessor must be scalar");
        switch (accessor.ComponentType)
        {
        case 5121u:
            return ReadBinaryValue<uint8_t>(accessor, element, 0u);
        case 5123u:
            return ReadBinaryValue<uint16_t>(accessor, element, 0u);
        case 5125u:
            return ReadBinaryValue<uint32_t>(accessor, element, 0u);
        default:
            Require(false,
                context + " index accessor must use an unsigned integer");
            return 0u;
        }
    }

    void CollectReferencedMeshes(
        const Json::Value& document,
        Json::ArrayIndex nodeIndex,
        std::set<Json::ArrayIndex>& visiting,
        std::set<Json::ArrayIndex>& visited,
        std::set<Json::ArrayIndex>& meshes,
        const std::string& context)
    {
        const Json::Value& nodes = document["nodes"];
        Require(nodeIndex < nodes.size(),
            context + " scene has an invalid node index");
        if (visited.find(nodeIndex) != visited.end())
            return;
        Require(visiting.insert(nodeIndex).second,
            context + " scene graph contains a cycle");

        const Json::Value& node = nodes[nodeIndex];
        Require(!node.isMember("matrix") &&
                !node.isMember("translation") &&
                !node.isMember("rotation") &&
                !node.isMember("scale"),
            context +
                " imported geometry must retain baked identity transforms");
        if (node.isMember("mesh"))
        {
            Require(node["mesh"].isUInt() &&
                    node["mesh"].asUInt() < document["meshes"].size(),
                context + " scene node has an invalid mesh index");
            meshes.insert(node["mesh"].asUInt());
        }

        const Json::Value& children = node["children"];
        if (!children.isNull())
        {
            Require(children.isArray(),
                context + " scene node children must be an array");
            for (const Json::Value& child : children)
            {
                Require(child.isUInt(),
                    context + " scene node has a nonnumeric child");
                CollectReferencedMeshes(
                    document,
                    child.asUInt(),
                    visiting,
                    visited,
                    meshes,
                    context);
            }
        }

        visiting.erase(nodeIndex);
        visited.insert(nodeIndex);
    }

    void ValidateInitialCameraGeometry(
        const GlbPayload& payload,
        const ExpectedScene& expected)
    {
        const std::string context = expected.DisplayName;
        const Json::Value& document = payload.Document;
        Require(document["nodes"].isArray() &&
                !document["nodes"].empty(),
            context + " GLB has no scene nodes");
        const Json::ArrayIndex sceneIndex =
            document.isMember("scene")
                ? document["scene"].asUInt()
                : 0u;
        Require(sceneIndex < document["scenes"].size(),
            context + " GLB has an invalid default scene");
        const Json::Value& sceneRoots =
            document["scenes"][sceneIndex]["nodes"];
        Require(sceneRoots.isArray() && !sceneRoots.empty(),
            context + " GLB default scene has no root nodes");

        std::set<Json::ArrayIndex> visiting;
        std::set<Json::ArrayIndex> visited;
        std::set<Json::ArrayIndex> meshes;
        for (const Json::Value& sceneRoot : sceneRoots)
        {
            Require(sceneRoot.isUInt(),
                context + " GLB has a nonnumeric root node");
            CollectReferencedMeshes(
                document,
                sceneRoot.asUInt(),
                visiting,
                visited,
                meshes,
                context);
        }
        Require(!meshes.empty(),
            context + " GLB default scene has no referenced meshes");

        const Float3 camera{
            expected.Position[0],
            expected.Position[1],
            expected.Position[2]
        };
        const Float3 forward = Normalize({
            expected.Direction[0],
            expected.Direction[1],
            expected.Direction[2]
        });
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

        for (const Json::ArrayIndex meshIndex : meshes)
        {
            const Json::Value& primitives =
                document["meshes"][meshIndex]["primitives"];
            Require(primitives.isArray() && !primitives.empty(),
                context + " mesh has no primitives");
            for (Json::ArrayIndex primitiveIndex = 0u;
                primitiveIndex < primitives.size();
                ++primitiveIndex)
            {
                const Json::Value& primitive =
                    primitives[primitiveIndex];
                Require(primitive.get("mode", 4u).asUInt() == 4u,
                    context + " camera contract supports triangle primitives only");
                const Json::Value& positionIndex =
                    primitive["attributes"]["POSITION"];
                Require(positionIndex.isUInt(),
                    context + " primitive has an invalid position accessor");
                const std::string primitiveContext =
                    context + " primitive " +
                    std::to_string(primitiveIndex);
                const AccessorView positions = GetAccessorView(
                    payload,
                    positionIndex.asUInt(),
                    primitiveContext + " positions");

                AccessorView indices;
                bool indexed = primitive.isMember("indices");
                uint64_t indexCount = positions.Count;
                if (indexed)
                {
                    Require(primitive["indices"].isUInt(),
                        primitiveContext + " has an invalid index accessor");
                    indices = GetAccessorView(
                        payload,
                        primitive["indices"].asUInt(),
                        primitiveContext + " indices");
                    indexCount = indices.Count;
                }
                Require(indexCount > 0u && indexCount % 3u == 0u,
                    primitiveContext +
                        " triangle index count must be nonzero and divisible by three");

                for (uint64_t index = 0u; index < indexCount; index += 3u)
                {
                    const uint64_t firstIndex = indexed
                        ? ReadIndex(indices, index, primitiveContext)
                        : index;
                    const uint64_t secondIndex = indexed
                        ? ReadIndex(indices, index + 1u, primitiveContext)
                        : index + 1u;
                    const uint64_t thirdIndex = indexed
                        ? ReadIndex(indices, index + 2u, primitiveContext)
                        : index + 2u;
                    Require(firstIndex < positions.Count &&
                            secondIndex < positions.Count &&
                            thirdIndex < positions.Count,
                        primitiveContext +
                            " triangle index exceeds the position accessor");
                    const Float3 first =
                        ReadPosition(positions, firstIndex, primitiveContext);
                    const Float3 second =
                        ReadPosition(positions, secondIndex, primitiveContext);
                    const Float3 third =
                        ReadPosition(positions, thirdIndex, primitiveContext);

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
        Require(camera.X > boundsMinimum.X &&
                camera.X < boundsMaximum.X &&
                camera.Y > boundsMinimum.Y &&
                camera.Y < boundsMaximum.Y &&
                camera.Z > boundsMinimum.Z &&
                camera.Z < boundsMaximum.Z,
            context + " initial camera is outside the scene bounds");

        const double sceneDiagonal = std::sqrt(
            LengthSquared(Subtract(boundsMaximum, boundsMinimum)));
        const double collisionRadius = std::max(
            0.1,
            std::max(sceneDiagonal, 100.0) * 0.0005);
        const double minimumClearance =
            std::sqrt(minimumDistanceSquared);
        Require(minimumClearance >= collisionRadius + 0.01,
            context +
                " initial camera intersects its collision sphere with scene geometry");
        Require(std::isfinite(nearestRayHits[0]) &&
                nearestRayHits[0] > collisionRadius &&
                nearestRayHits[0] <= 3.0,
            context +
                " initial camera must have a reachable floor within three meters");
        for (size_t rayIndex = 1u; rayIndex <= 4u; ++rayIndex)
        {
            Require(std::isfinite(nearestRayHits[rayIndex]) &&
                    nearestRayHits[rayIndex] > collisionRadius &&
                    nearestRayHits[rayIndex] <= sceneDiagonal * 1.01,
                context +
                    " initial camera must be enclosed by scene geometry on four horizontal sides");
        }
        Require(std::isfinite(nearestRayHits[5]) &&
                nearestRayHits[5] > collisionRadius &&
                nearestRayHits[5] <= sceneDiagonal * 1.01,
            context +
                " initial camera must face reachable scene geometry");

        std::cout << context
                  << " initial camera: "
                  << minimumClearance
                  << " m geometry clearance, "
                  << nearestRayHits[0]
                  << " m above floor\n";
    }
}

int main(int argumentCount, char** arguments)
{
    try
    {
        ValidateAccessorByteRangeArithmetic();
        Require(argumentCount == 3,
            "usage: uvsr_scene_asset_contract_tests <source-root> <staging-root>");
        const std::filesystem::path sourceRoot =
            std::filesystem::path(arguments[1]).lexically_normal();
        const std::filesystem::path stagingRoot =
            std::filesystem::path(arguments[2]).lexically_normal();
        Require(std::filesystem::is_directory(sourceRoot), "source scene root is missing");
        Require(std::filesystem::is_directory(stagingRoot), "staged scene root is missing");

        donut::vfs::NativeFileSystem fileSystem;
        std::vector<std::string> discovered;
        for (const auto& item : std::filesystem::recursive_directory_iterator(sourceRoot))
        {
            if (item.is_regular_file() && IsLoadableSceneFile(item.path()))
                discovered.push_back(item.path().lexically_normal().generic_string());
        }
        const auto catalog = uvsr::BuildSceneCatalog(fileSystem, sourceRoot, discovered);
        std::set<std::string> catalogDisplayNames;
        for (const auto& entry : catalog)
            catalogDisplayNames.insert(entry.DisplayName);
        const std::set<std::string> expectedCatalogDisplayNames(
            kExpectedCatalogDisplayNames.begin(),
            kExpectedCatalogDisplayNames.end());
        Require(catalog.size() == kExpectedCatalogDisplayNames.size() &&
                catalogDisplayNames.size() == catalog.size() &&
                catalogDisplayNames == expectedCatalogDisplayNames,
            "scene picker inventory differs from the four supported runtime scenes");

        for (const char* removedSceneSlug : kRemovedSceneSlugs)
        {
            Require(!std::filesystem::exists(sourceRoot / removedSceneSlug),
                std::string("removed scene remains in source assets: ") +
                    removedSceneSlug);
            Require(!std::filesystem::exists(stagingRoot / removedSceneSlug),
                std::string("removed scene remains in staged assets: ") +
                    removedSceneSlug);
        }

        std::set<std::string> displayNames;
        for (const ExpectedScene& expected : kExpectedScenes)
        {
            const std::filesystem::path descriptor =
                (sourceRoot / expected.Descriptor).lexically_normal();
            Require(std::filesystem::is_regular_file(descriptor),
                "missing scene descriptor: " + descriptor.generic_string());

            Json::Value document;
            Require(donut::json::LoadFromFile(
                fileSystem,
                descriptor.generic_string(),
                document),
                "failed to parse scene descriptor: " + descriptor.generic_string());
            Require(document["displayName"].asString() == expected.DisplayName,
                "scene descriptor display name differs: " + descriptor.generic_string());
            Require(displayNames.insert(expected.DisplayName).second,
                "downloaded scene display names must be unique");
            Require(document["models"].isArray() && !document["models"].empty(),
                "scene descriptor has no models: " + descriptor.generic_string());
            Require(document["models"].size() == 1u,
                "downloaded scene descriptor must reference one merged model: "
                    + descriptor.generic_string());
            Require(document["graph"].isArray()
                    && document["graph"].size() == document["models"].size(),
                "scene descriptor graph must instantiate every model: "
                    + descriptor.generic_string());
            const Json::Value& initialCamera =
                document["initialCamera"];
            Require(initialCamera.isObject(),
                "scene descriptor has no initial camera: "
                    + descriptor.generic_string());
            const auto position = ReadFloat3(
                initialCamera["position"],
                "initial camera position");
            const auto direction = ReadFloat3(
                initialCamera["direction"],
                "initial camera direction");
            const auto up = ReadFloat3(
                initialCamera["up"],
                "initial camera up");
            Require(NearlyEqual(position, expected.Position),
                "scene descriptor initial camera position differs: "
                    + descriptor.generic_string());
            Require(NearlyEqual(direction, expected.Direction),
                "scene descriptor initial camera direction differs: "
                    + descriptor.generic_string());
            Require(NearlyEqual(up, { 0.f, 1.f, 0.f }),
                "scene descriptor initial camera must remain Y-up: "
                    + descriptor.generic_string());
            Require(
                initialCamera["verticalFovDegrees"].isNumeric() &&
                    NearlyEqual(
                        initialCamera["verticalFovDegrees"].asFloat(),
                        60.f),
                "scene descriptor initial camera FOV differs: "
                    + descriptor.generic_string());
            for (const Json::Value& graphNode : document["graph"])
            {
                Require(graphNode["model"].isUInt()
                        && graphNode["model"].asUInt() < document["models"].size(),
                    "scene descriptor graph has an invalid model index: "
                        + descriptor.generic_string());
            }

            const auto* catalogEntry =
                uvsr::FindSceneCatalogEntry(catalog, descriptor.generic_string());
            Require(catalogEntry != nullptr,
                "scene descriptor is missing from the picker: " + descriptor.generic_string());
            Require(catalogEntry->DisplayName == expected.DisplayName,
                "scene picker display name differs: " + descriptor.generic_string());
            Require(catalogEntry->InitialCamera.has_value(),
                "scene picker discarded the initial camera: "
                    + descriptor.generic_string());
            Require(
                NearlyEqual(
                    catalogEntry->InitialCamera->Position,
                    expected.Position) &&
                    NearlyEqual(
                        catalogEntry->InitialCamera->Direction,
                        expected.Direction),
                "scene picker initial camera differs: "
                    + descriptor.generic_string());

            const std::filesystem::path stagedDescriptor =
                stagingRoot / descriptor.lexically_relative(sourceRoot);
            RequireFilesEqual(descriptor, stagedDescriptor);

            for (const Json::Value& model : document["models"])
            {
                Require(model.isString() && !model.asString().empty(),
                    "scene descriptor contains an invalid model reference");
                const std::filesystem::path relativeModel(model.asString());
                Require(!relativeModel.is_absolute(),
                    "scene descriptor model reference must be relative");
                const std::filesystem::path component =
                    (descriptor.parent_path() / relativeModel).lexically_normal();
                Require(IsContainedBy(sourceRoot, component),
                    "scene descriptor model escapes the scene root");
                Require(std::filesystem::is_regular_file(component),
                    "missing scene component: " + component.generic_string());
                Require(EndsWithCaseInsensitive(component.generic_string(), ".glb"),
                    "downloaded scene component must be GLB");
                const GlbPayload payload = ValidateGlb(component);
                ValidateInitialCameraGeometry(payload, expected);
                Require(uvsr::FindSceneCatalogEntry(catalog, component.generic_string()) == nullptr,
                    "descriptor-owned component is visible in the scene picker");

                const std::filesystem::path stagedComponent =
                    stagingRoot / component.lexically_relative(sourceRoot);
                RequireFilesEqual(component, stagedComponent);
            }
        }

        std::cout << "downloaded scene asset contract tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "downloaded scene asset contract tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
