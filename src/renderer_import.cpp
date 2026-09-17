#include "renderer_import.h"
#include "import/renderer_import_private.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <simdjson.h>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>

namespace uvsr
{
    template<class T, fastgltf::AccessorType Shape>
    struct ImportElement { T values[fastgltf::getNumComponents(Shape)]; };
}

namespace fastgltf
{
    template<class T, AccessorType Shape>
    struct ElementTraits<uvsr::ImportElement<T, Shape>>
        : ElementTraitsBase<uvsr::ImportElement<T, Shape>, Shape, T> {};
}

namespace uvsr
{
    namespace
    {
#if defined(UVSR_BUILD_TESTING)
        int64_t allocationCountdown = -1;
#endif
        ImportResult Fail(ImportError error, ImportObject object = ImportObject::Document,
            size_t index = SIZE_MAX) noexcept { return {error, object, index}; }

        bool RangeFits(size_t offset, size_t count, size_t stride, size_t lastSize, size_t length) noexcept
        {
            return count > 0 && offset <= length && lastSize <= length - offset &&
                (count == 1 || (stride > 0 && count - 1 <= (length - offset - lastSize) / stride));
        }

        uint32_t ReadLittleUnsigned(const uint8_t* bytes, size_t size) noexcept
        {
            uint32_t value = 0;
            for (size_t i = 0; i < size; ++i) value |= uint32_t(bytes[i]) << (8 * i);
            return value;
        }

        ImportResult ParserFailure(fastgltf::Error error) noexcept
        {
            ImportError mapped = ImportError::InvalidData;
            switch (error)
            {
            case fastgltf::Error::None: return {};
            case fastgltf::Error::InvalidJson: mapped = ImportError::InvalidJson; break;
            case fastgltf::Error::InvalidGLB: mapped = ImportError::InvalidContainer; break;
            case fastgltf::Error::UnsupportedVersion: mapped = ImportError::UnsupportedVersion; break;
            case fastgltf::Error::MissingExtensions:
            case fastgltf::Error::UnknownRequiredExtension: mapped = ImportError::UnsupportedExtension; break;
            case fastgltf::Error::InvalidURI: mapped = ImportError::InvalidUri; break;
            case fastgltf::Error::FileBufferAllocationFailed: mapped = ImportError::OutOfMemory; break;
            default: break;
            }
            return {mapped, ImportObject::Document, SIZE_MAX, static_cast<uint32_t>(error)};
        }

        ImportResult JsonBytes(ArrayView<const uint8_t> input, ArrayView<const uint8_t>& json, bool& binary) noexcept
        {
            if (!input.IsValid() || input.count == 0) return Fail(ImportError::InvalidInput);
            if (input.count > SIZE_MAX - simdjson::SIMDJSON_PADDING || input.count > simdjson::SIMDJSON_MAXSIZE_BYTES)
                return Fail(ImportError::Overflow);
            binary = input.count >= 4 && ReadLittleUnsigned(input.data, 4) == 0x46546c67;
            json = input;
            if (!binary) return {};
            if (input.count < 20 || ReadLittleUnsigned(input.data + 8, 4) != input.count)
                return Fail(ImportError::InvalidContainer);
            if (ReadLittleUnsigned(input.data + 4, 4) != 2) return Fail(ImportError::UnsupportedVersion);
            size_t cursor = 12;
            size_t chunk = 0;
            bool hasBinaryChunk = false;
            while (cursor < input.count)
            {
                if (input.count - cursor < 8) return Fail(ImportError::InvalidContainer);
                const size_t size = ReadLittleUnsigned(input.data + cursor, 4);
                const uint32_t type = ReadLittleUnsigned(input.data + cursor + 4, 4);
                cursor += 8;
                if (size % 4 || size > input.count - cursor) return Fail(ImportError::InvalidContainer);
                if (chunk == 0)
                {
                    if (type != 0x4e4f534a || size == 0) return Fail(ImportError::InvalidContainer);
                    json = {input.data + cursor, size};
                }
                else if (type == 0x4e4f534a || (type == 0x004e4942 && chunk != 1))
                    return Fail(ImportError::InvalidContainer);
                else if (type == 0x004e4942) hasBinaryChunk = true;
                cursor += size;
                ++chunk;
            }
            // unknown chunks are ignorable. the vendor binary parser expects a BIN
            // second chunk, so a JSON-only GLB goes through its JSON entry point.
            binary = hasBinaryChunk;
            return {};
        }

        bool ValidDataUri(std::string_view text) noexcept
        {
            if (text.substr(0, 5) != "data:") return true;
            const size_t comma = text.find(',');
            if (comma == std::string_view::npos || comma < 7 || text.substr(comma - 7, 7) != ";base64")
                return false;
            const auto data = text.substr(comma + 1);
            if (data.size() < 4 || data.size() % 4) return false;
            const size_t padding = data.back() == '=' ? (data[data.size() - 2] == '=' ? 2 : 1) : 0;
            for (size_t i = 0; i < data.size() - padding; ++i)
            {
                const char c = data[i];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '+' || c == '/')) return false;
            }
            return true;
        }

        ImportError ValidateUri(std::string_view uri) noexcept
        {
            if (uri.empty() || !ValidDataUri(uri)) return ImportError::InvalidUri;
            // fastgltf reparses percent-decoded paths. check both spellings without
            // allocating another path string or allowing encoded delimiters to bypass checks.
            for (unsigned decode = 0; decode < 2; ++decode)
            {
                size_t colon = SIZE_MAX;
                size_t decodedCount = 0;
                size_t authorityStart = SIZE_MAX;
                size_t authoritySlash = SIZE_MAX;
                char prefix[5]{};
                unsigned slashes = 0;
                for (size_t i = 0; i < uri.size(); ++i, ++decodedCount)
                {
                    unsigned value = static_cast<unsigned char>(uri[i]);
                    if (decode && value == '%')
                    {
                        if (uri.size() - i < 3) return ImportError::InvalidUri;
                        value = 0;
                        for (size_t j = 1; j <= 2; ++j)
                        {
                            const char c = uri[i+j];
                            if (c >= '0' && c <= '9') value = value * 16 + unsigned(c - '0');
                            else if (c >= 'a' && c <= 'f') value = value * 16 + unsigned(c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F') value = value * 16 + unsigned(c - 'A' + 10);
                            else return ImportError::InvalidUri;
                        }
                        i += 2;
                    }
                    if (value == 0) return ImportError::InvalidUri;
                    if (decodedCount < 5) prefix[decodedCount] = char(value);
                    if (value == ':' && colon == SIZE_MAX)
                    {
                        if (decodedCount == 0) return ImportError::InvalidUri;
                        colon = decodedCount;
                        slashes = 0;
                        authorityStart = authoritySlash = SIZE_MAX;
                        continue;
                    }
                    const size_t path = colon == SIZE_MAX ? 0 : colon + 1;
                    if (decodedCount == path || decodedCount == path + 1)
                    {
                        if (value == '/') ++slashes;
                        if (decodedCount == path + 1 && slashes == 2) authorityStart = path + 2;
                    }
                    else if (authorityStart != SIZE_MAX && authoritySlash == SIZE_MAX && value == '/')
                        authoritySlash = decodedCount;
                }
                const bool file = colon == 4 && memcmp(prefix,"file:",5) == 0;
                if (authorityStart != SIZE_MAX && (authoritySlash == SIZE_MAX ||
                    (authoritySlash == authorityStart && !file))) return ImportError::InvalidUri;
            }
            return ImportError::None;
        }

        bool ValidComponent(uint64_t value) noexcept
        {
            return value == 5120 || value == 5121 || value == 5122 || value == 5123 ||
                value == 5125 || value == 5126;
        }

        bool Version(std::string_view text, uint32_t& major, uint32_t& minor) noexcept
        {
            uint32_t parts[2]{};
            size_t part = 0;
            size_t digits = 0;
            for (char c : text)
            {
                if (c == '.' && part == 0 && digits > 0) { ++part; digits = 0; continue; }
                if (c < '0' || c > '9' || parts[part] > (UINT32_MAX - unsigned(c - '0')) / 10)
                    return false;
                parts[part] = parts[part] * 10 + unsigned(c - '0');
                ++digits;
            }
            major = parts[0]; minor = parts[1];
            return part == 1 && digits > 0;
        }

        // this bounded preflight protects vendor decoding assumptions before parsing.
        // its DOM is destroyed before fastgltf creates the retained import asset.
        ImportResult Preflight(ArrayView<const uint8_t> json, ImportMetadata& metadata) noexcept
        {
            simdjson::dom::parser parser;
            simdjson::dom::object root;
            auto error = parser.parse(json.data, json.count).get(root);
            if (error != simdjson::SUCCESS)
                return Fail(error == simdjson::MEMALLOC ? ImportError::OutOfMemory : ImportError::InvalidJson);
            std::string_view version;
            if (root["asset"]["version"].get_string().get(version) == simdjson::SUCCESS)
            {
                uint32_t major = 0, minor = 0;
                if (!Version(version,major,minor)) return Fail(ImportError::InvalidData);
                if (major != 2) return Fail(ImportError::UnsupportedVersion);
            }
            error = root["asset"]["minVersion"].get_string().get(version);
            if (error != simdjson::NO_SUCH_FIELD)
            {
                uint32_t major = 0, minor = 0;
                if (error != simdjson::SUCCESS || !Version(version,major,minor)) return Fail(ImportError::InvalidData);
                if (major > 2 || (major == 2 && minor > 0)) return Fail(ImportError::UnsupportedVersion);
            }
            const char* extensionKeys[]{"extensionsRequired","extensionsUsed"};
            for (const auto* key : extensionKeys)
            {
                simdjson::dom::array list;
                error = root[key].get_array().get(list);
                if (error == simdjson::NO_SUCH_FIELD) continue;
                if (error != simdjson::SUCCESS) return Fail(ImportError::InvalidData);
                for (auto value : list)
                {
                    std::string_view name;
                    if (value.get_string().get(name) != simdjson::SUCCESS) return Fail(ImportError::InvalidData);
                }
            }
            if (auto result = ReadImportMaterialMetadata(root, json, metadata); !result) return result;
            if (auto result = ReadImportSceneMetadata(root, metadata); !result) return result;
            const char* resourceKeys[]{"buffers", "images"};
            for (const auto* key : resourceKeys)
            {
                simdjson::dom::array list;
                error = root[key].get_array().get(list);
                if (error == simdjson::NO_SUCH_FIELD) continue;
                const ImportObject object = key[0] == 'b' ? ImportObject::Buffer : ImportObject::Image;
                if (error != simdjson::SUCCESS) return Fail(ImportError::InvalidData, object);
                size_t index = 0;
                for (auto item : list)
                {
                    std::string_view uri;
                    error = item["uri"].get_string().get(uri);
                    if (error != simdjson::NO_SUCH_FIELD &&
                        (error != simdjson::SUCCESS || ValidateUri(uri) != ImportError::None))
                        return Fail(ImportError::InvalidUri, object, index);
                    ++index;
                }
            }
            simdjson::dom::array nodeList;
            error = root["nodes"].get_array().get(nodeList);
            if (error != simdjson::NO_SUCH_FIELD)
            {
                if (error != simdjson::SUCCESS) return Fail(ImportError::InvalidData, ImportObject::Node);
                metadata.nodeCount = nodeList.size();
                if (metadata.nodeCount > PTRDIFF_MAX) return Fail(ImportError::Overflow, ImportObject::Node);
                if (metadata.nodeCount)
                {
                    metadata.nodeFlags = static_cast<uint8_t*>(ImportAllocate(metadata.nodeCount));
                    if (!metadata.nodeFlags) return Fail(ImportError::OutOfMemory, ImportObject::Node);
                }
                size_t nodeIndex = 0;
                for (auto item : nodeList)
                {
                    simdjson::dom::object object;
                    if (item.get_object().get(object)) return Fail(ImportError::InvalidData, ImportObject::Node, nodeIndex);
                    simdjson::dom::element value;
                    const bool matrix = object["matrix"].get(value) == simdjson::SUCCESS;
                    const bool trs = object["translation"].get(value) == simdjson::SUCCESS ||
                        object["rotation"].get(value) == simdjson::SUCCESS || object["scale"].get(value) == simdjson::SUCCESS;
                    if (matrix && trs) return Fail(ImportError::InvalidData, ImportObject::Node, nodeIndex);
                    uint8_t flags = matrix || trs ? ImportNodeTransform : 0;
                    if (object["name"].get(value) == simdjson::SUCCESS) flags |= ImportNodeName;
                    uint64_t camera = 0;
                    error = object["camera"].get_uint64().get(camera);
                    if (error != simdjson::NO_SUCH_FIELD && (error != simdjson::SUCCESS || camera >= UINT32_MAX))
                        return Fail(ImportError::InvalidData, ImportObject::Camera, nodeIndex);
                    simdjson::dom::object extensions, light;
                    error = object["extensions"].get_object().get(extensions);
                    if (error != simdjson::SUCCESS && error != simdjson::NO_SUCH_FIELD)
                        return Fail(ImportError::InvalidData, ImportObject::Node, nodeIndex);
                    if (error == simdjson::SUCCESS)
                    {
                        error = extensions["KHR_lights_punctual"].get_object().get(light);
                        if (error != simdjson::SUCCESS && error != simdjson::NO_SUCH_FIELD)
                            return Fail(ImportError::InvalidData, ImportObject::Light, nodeIndex);
                        if (error == simdjson::SUCCESS)
                        {
                            uint64_t index = 0;
                            if (light["light"].get_uint64().get(index) != simdjson::SUCCESS || index >= UINT32_MAX)
                                return Fail(ImportError::InvalidData, ImportObject::Light, nodeIndex);
                            flags |= ImportNodeLight;
                        }
                    }
                    new (&metadata.nodeFlags[nodeIndex++]) uint8_t(flags);
                }
            }
            simdjson::dom::array accessors;
            error = root["accessors"].get_array().get(accessors);
            if (error == simdjson::NO_SUCH_FIELD) return {};
            if (error != simdjson::SUCCESS) return Fail(ImportError::InvalidAccessor);
            size_t index = 0;
            for (auto item : accessors)
            {
                uint64_t component = 0;
                if (item["componentType"].get_uint64().get(component) || !ValidComponent(component))
                    return Fail(ImportError::InvalidAccessor, ImportObject::Accessor, index);
                // the pinned parser otherwise narrows out-of-range floating bounds to int64.
                uint64_t view = 0;
                error = item["bufferView"].get_uint64().get(view);
                if (error != simdjson::SUCCESS && error != simdjson::NO_SUCH_FIELD)
                    return Fail(ImportError::InvalidAccessor, ImportObject::Accessor, index);
                const char* boundKeys[]{"min", "max"};
                for (const auto* key : boundKeys)
                {
                    simdjson::dom::array values;
                    error = item[key].get_array().get(values);
                    if (error == simdjson::NO_SUCH_FIELD) continue;
                    if (error != simdjson::SUCCESS) return Fail(ImportError::InvalidAccessor, ImportObject::Accessor, index);
                    for (auto value : values)
                    {
                        double number = 0;
                        if (value.get_double().get(number) || !isfinite(number) ||
                            (component != 5126 && (number < -2147483648.0 || number > 4294967295.0)))
                            return Fail(ImportError::InvalidAccessor, ImportObject::Accessor, index);
                    }
                }
                simdjson::dom::element sparse;
                error = item["sparse"].get(sparse);
                if (error != simdjson::NO_SUCH_FIELD)
                {
                    uint64_t type = 0;
                    if (error || sparse["indices"]["componentType"].get_uint64().get(type) ||
                        (type != 5121 && type != 5123 && type != 5125))
                        return Fail(ImportError::InvalidSparse, ImportObject::Accessor, index);
                }
                ++index;
            }
            return {};
        }

    }

    void* ImportAllocate(size_t bytes) noexcept
    {
#if defined(UVSR_BUILD_TESTING)
        if (allocationCountdown == 0) return nullptr;
        if (allocationCountdown > 0) --allocationCountdown;
#endif
        return malloc(bytes);
    }

    ImportState::ImportState(fastgltf::Asset&& input) noexcept : asset(std::move(input)) {}
    ImportState::~ImportState() noexcept
    {
        if (buffers)
            for (size_t i = 0; i < asset.buffers.size(); ++i) free(buffers[i].owned);
        free(buffers);
    }

    namespace
    {
        void Destroy(ImportState* state) noexcept
        {
            if (state) { state->~ImportState(); free(state); }
        }

        ImportResult Describe(const fastgltf::Accessor& accessor, size_t index, ImportAccessorInfo& info) noexcept
        {
            info = {};
            size_t components = 0;
            switch (accessor.type)
            {
            case fastgltf::AccessorType::Scalar: info.shape = ImportShape::Scalar; components = 1; break;
            case fastgltf::AccessorType::Vec2: info.shape = ImportShape::Vec2; components = 2; break;
            case fastgltf::AccessorType::Vec3: info.shape = ImportShape::Vec3; components = 3; break;
            case fastgltf::AccessorType::Vec4: info.shape = ImportShape::Vec4; components = 4; break;
            case fastgltf::AccessorType::Mat2: info.shape = ImportShape::Mat2; components = 4; break;
            case fastgltf::AccessorType::Mat3: info.shape = ImportShape::Mat3; components = 9; break;
            case fastgltf::AccessorType::Mat4: info.shape = ImportShape::Mat4; components = 16; break;
            default: return Fail(ImportError::InvalidAccessor, ImportObject::Accessor, index);
            }
            switch (accessor.componentType)
            {
            case fastgltf::ComponentType::Byte: info.component = ImportComponent::Int8; break;
            case fastgltf::ComponentType::UnsignedByte: info.component = ImportComponent::Uint8; break;
            case fastgltf::ComponentType::Short: info.component = ImportComponent::Int16; break;
            case fastgltf::ComponentType::UnsignedShort: info.component = ImportComponent::Uint16; break;
            case fastgltf::ComponentType::UnsignedInt: info.component = ImportComponent::Uint32; break;
            case fastgltf::ComponentType::Float: info.component = ImportComponent::Float32; break;
            default: return Fail(ImportError::InvalidAccessor, ImportObject::Accessor, index);
            }
            if (accessor.count == 0) return Fail(ImportError::InvalidAccessor, ImportObject::Accessor, index);
            if (accessor.count > SIZE_MAX / components / sizeof(float))
                return Fail(ImportError::Overflow, ImportObject::Accessor, index);
            info.count = accessor.count;
            info.scalarCount = accessor.count * components;
            info.normalized = accessor.normalized;
            info.sparse = accessor.sparse.has_value();
            return {};
        }

        ImportResult AccessorRange(const ImportState& state, size_t index, size_t viewIndex,
            size_t offset, size_t count, size_t stride, size_t lastSize,
            size_t componentAlignment, size_t startAlignment, bool sparse) noexcept
        {
            if (viewIndex >= state.asset.bufferViews.size())
                return Fail(ImportError::InvalidIndex, ImportObject::Accessor, index);
            const auto& view = state.asset.bufferViews[viewIndex];
            if (sparse && (view.byteStride || view.target))
                return Fail(ImportError::InvalidSparse, ImportObject::Accessor, index);
            if (!RangeFits(offset, count, stride, lastSize, view.byteLength) ||
                offset % componentAlignment || (view.byteOffset + offset) % startAlignment || stride % startAlignment)
                return Fail(ImportError::InvalidRange, ImportObject::Accessor, index);
            return {};
        }

        ImportResult ValidateLayout(const ImportState& state, size_t index) noexcept
        {
            const auto& accessor = state.asset.accessors[index];
            ImportAccessorInfo info;
            if (auto result = Describe(accessor, index, info); !result) return result;
            const size_t component = fastgltf::getComponentByteSize(accessor.componentType);
            const bool matrix = fastgltf::isMatrix(accessor.type);
            const size_t rows = fastgltf::getElementRowCount(accessor.type);
            const size_t alignment = matrix && component < 4 ? 4 : component;
            const size_t element = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
            const size_t tailPadding = matrix ? (4 - (rows * component) % 4) % 4 : 0;
            const size_t lastSize = element - tailPadding;
            if (accessor.bufferViewIndex)
            {
                const size_t viewIndex = *accessor.bufferViewIndex;
                if (viewIndex >= state.asset.bufferViews.size())
                    return Fail(ImportError::InvalidIndex, ImportObject::Accessor, index);
                const auto& view = state.asset.bufferViews[viewIndex];
                const size_t stride = view.byteStride.value_or(element);
                if (stride < element) return Fail(ImportError::InvalidRange, ImportObject::Accessor, index);
                if (auto result = AccessorRange(state, index, viewIndex, accessor.byteOffset,
                    accessor.count, stride, lastSize, component, alignment, false); !result) return result;
            }
            else if (accessor.byteOffset) return Fail(ImportError::InvalidRange, ImportObject::Accessor, index);
            if (accessor.sparse)
            {
                const auto& sparse = *accessor.sparse;
                if (sparse.count == 0 || sparse.count > accessor.count)
                    return Fail(ImportError::InvalidSparse, ImportObject::Accessor, index);
                const size_t indexSize = fastgltf::getComponentByteSize(sparse.indexComponentType);
                if (auto result = AccessorRange(state, index, sparse.indicesBufferView, sparse.indicesByteOffset,
                    sparse.count, indexSize, indexSize, indexSize, indexSize, true); !result) return result;
                if (auto result = AccessorRange(state, index, sparse.valuesBufferView, sparse.valuesByteOffset,
                    sparse.count, element, lastSize, component, alignment, true); !result) return result;
            }
            return {};
        }

        struct BufferAdapter
        {
            const ImportState& state;
            fastgltf::span<const std::byte> operator()(const fastgltf::Asset&, size_t index) const noexcept
            {
                const auto& view = state.asset.bufferViews[index];
                const auto& buffer = state.buffers[view.bufferIndex].bytes;
                return fastgltf::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(buffer.data + view.byteOffset), view.byteLength);
            }
        };

        ImportResult ResidentAccessor(const ImportState& state, size_t index) noexcept
        {
            const auto& accessor = state.asset.accessors[index];
            size_t views[3];
            size_t count = 0;
            if (accessor.bufferViewIndex) views[count++] = *accessor.bufferViewIndex;
            if (accessor.sparse)
            {
                views[count++] = accessor.sparse->indicesBufferView;
                views[count++] = accessor.sparse->valuesBufferView;
            }
            for (size_t i = 0; i < count; ++i)
                if (!state.buffers[state.asset.bufferViews[views[i]].bufferIndex].bytes.data)
                    return Fail(ImportError::BufferUnavailable, ImportObject::Accessor, index);
            if (accessor.sparse)
            {
                const auto& sparse = *accessor.sparse;
                const auto bytes = BufferAdapter{state}(state.asset, sparse.indicesBufferView);
                const size_t stride = fastgltf::getComponentByteSize(sparse.indexComponentType);
                const auto* indices = reinterpret_cast<const uint8_t*>(bytes.data()) + sparse.indicesByteOffset;
                uint32_t previous = 0;
                for (size_t i = 0; i < sparse.count; ++i)
                {
                    const uint32_t value = ReadLittleUnsigned(indices + i * stride, stride);
                    if (value >= accessor.count || (i > 0 && value <= previous))
                        return Fail(ImportError::InvalidSparse, ImportObject::Accessor, index);
                    previous = value;
                }
            }
            return {};
        }

        ImportResult Initialize(ImportState& state) noexcept
        {
            const size_t count = state.asset.buffers.size();
            if (count > SIZE_MAX / sizeof(ImportBufferStorage)) return Fail(ImportError::Overflow);
            if (count)
            {
                state.buffers = static_cast<ImportBufferStorage*>(ImportAllocate(count * sizeof(ImportBufferStorage)));
                if (!state.buffers) return Fail(ImportError::OutOfMemory);
                for (size_t i = 0; i < count; ++i) new (&state.buffers[i]) ImportBufferStorage{};
            }
            for (size_t i = 0; i < count; ++i)
            {
                const auto& buffer = state.asset.buffers[i];
                if (!buffer.byteLength) return Fail(ImportError::InvalidRange, ImportObject::Buffer, i);
                if (const auto* source = std::get_if<fastgltf::sources::Array>(&buffer.data))
                {
                    if (source->bytes.size_bytes() < buffer.byteLength)
                        return Fail(ImportError::InvalidRange, ImportObject::Buffer, i);
                    state.buffers[i].bytes = {reinterpret_cast<const uint8_t*>(source->bytes.data()), buffer.byteLength};
                }
                else if (!std::get_if<fastgltf::sources::URI>(&buffer.data))
                    return Fail(ImportError::UnsupportedData, ImportObject::Buffer, i);
            }
            for (size_t i = 0; i < state.asset.bufferViews.size(); ++i)
            {
                const auto& view = state.asset.bufferViews[i];
                if (view.bufferIndex >= count) return Fail(ImportError::InvalidIndex, ImportObject::BufferView, i);
                const size_t length = state.asset.buffers[view.bufferIndex].byteLength;
                if (view.byteLength == 0 || view.byteOffset > length || view.byteLength > length - view.byteOffset ||
                    (view.byteStride && (*view.byteStride < 4 || *view.byteStride > 252 || *view.byteStride % 4)))
                    return Fail(ImportError::InvalidRange, ImportObject::BufferView, i);
                if (view.meshoptCompression) return Fail(ImportError::UnsupportedData, ImportObject::BufferView, i);
            }
            for (size_t i = 0; i < state.asset.accessors.size(); ++i)
                if (auto result = ValidateLayout(state, i); !result) return result;
            return {};
        }

        template<class T, fastgltf::AccessorType Shape>
        ImportResult Decode(const ImportState& state, size_t index, ArrayView<T> output, bool checkFinite) noexcept
        {
            using Element = ImportElement<T, Shape>;
            constexpr size_t components = fastgltf::getNumComponents(Shape);
            const auto& accessor = state.asset.accessors[index];
            const BufferAdapter adapter{state};
            if (checkFinite)
            {
                bool finite = true;
                fastgltf::iterateAccessor<Element>(state.asset, accessor, [&](const Element& value) {
                    for (size_t i = 0; i < components; ++i) finite = finite && isfinite(value.values[i]);
                }, adapter);
                if (!finite) return Fail(ImportError::NonFiniteValue, ImportObject::Accessor, index);
            }
            size_t offset = 0;
            fastgltf::iterateAccessor<Element>(state.asset, accessor, [&](const Element& value) {
                for (size_t i = 0; i < components; ++i) output.data[offset++] = value.values[i];
            }, adapter);
            return {};
        }

        template<class T>
        ImportResult Read(const ImportState* state, size_t index, ArrayView<T> output, bool floats) noexcept
        {
            if (!state || index >= state->asset.accessors.size()) return Fail(ImportError::InvalidIndex, ImportObject::Accessor, index);
            const auto& accessor = state->asset.accessors[index];
            ImportAccessorInfo info;
            if (auto result = Describe(accessor, index, info); !result) return result;
            if (!output.IsValid() || output.count != info.scalarCount) return Fail(ImportError::InvalidOutput, ImportObject::Accessor, index);
            if (!floats && (accessor.normalized || fastgltf::isMatrix(accessor.type) ||
                (info.component != ImportComponent::Uint8 && info.component != ImportComponent::Uint16 && info.component != ImportComponent::Uint32)))
                return Fail(ImportError::InvalidAccessor, ImportObject::Accessor, index);
            if (auto result = ResidentAccessor(*state, index); !result) return result;
            const bool finite = floats && info.component == ImportComponent::Float32;
            switch (accessor.type)
            {
            case fastgltf::AccessorType::Scalar: return Decode<T, fastgltf::AccessorType::Scalar>(*state, index, output, finite);
            case fastgltf::AccessorType::Vec2: return Decode<T, fastgltf::AccessorType::Vec2>(*state, index, output, finite);
            case fastgltf::AccessorType::Vec3: return Decode<T, fastgltf::AccessorType::Vec3>(*state, index, output, finite);
            case fastgltf::AccessorType::Vec4: return Decode<T, fastgltf::AccessorType::Vec4>(*state, index, output, finite);
            case fastgltf::AccessorType::Mat2: return Decode<T, fastgltf::AccessorType::Mat2>(*state, index, output, finite);
            case fastgltf::AccessorType::Mat3: return Decode<T, fastgltf::AccessorType::Mat3>(*state, index, output, finite);
            case fastgltf::AccessorType::Mat4: return Decode<T, fastgltf::AccessorType::Mat4>(*state, index, output, finite);
            default: return Fail(ImportError::InvalidAccessor, ImportObject::Accessor, index);
            }
        }
    }

    ImportDocument::~ImportDocument() noexcept { Reset(); }
    ImportDocument::ImportDocument(ImportDocument&& other) noexcept : m_State(other.m_State) { other.m_State = nullptr; }
    ImportDocument& ImportDocument::operator=(ImportDocument&& other) noexcept
    {
        if (this != &other) { Reset(); m_State = other.m_State; other.m_State = nullptr; }
        return *this;
    }
    void ImportDocument::Reset() noexcept { Destroy(m_State); m_State = nullptr; }

    ImportResult ImportDocument::Parse(ArrayView<const uint8_t> bytes) noexcept
    {
        ArrayView<const uint8_t> json;
        bool binary = false;
        if (auto result = JsonBytes(bytes, json, binary); !result) return result;
        ImportMetadata metadata;
        if (auto result = Preflight(json, metadata); !result) return result;
        const auto input = binary ? bytes : json;
        auto data = fastgltf::GltfDataBuffer::FromBytes(reinterpret_cast<const std::byte*>(input.data), input.count);
        if (data.error() != fastgltf::Error::None) return ParserFailure(data.error());
        if (metadata.requiredKeyLength)
        {
            // the preflight has validated every required name and its supported
            // fields. hide only this root key from the vendor's fixed name list.
            // edit its owned copy, keeping JSON/GLB lengths and input bytes intact.
            auto owned = static_cast<fastgltf::span<std::byte>>(data.get());
            const size_t offset = size_t(json.data - input.data) + metadata.requiredKeyOffset;
            if (offset > owned.size() || metadata.requiredKeyLength > owned.size() - offset || metadata.requiredKeyLength < 3)
                return Fail(ImportError::InvalidState);
            auto* key = reinterpret_cast<uint8_t*>(owned.data()) + offset;
            memset(key, ' ', metadata.requiredKeyLength);
            key[0] = '"'; key[1] = '_'; key[2] = '"';
        }
        fastgltf::Parser parser(static_cast<fastgltf::Extensions>(ImportParserExtensions()));
        auto parsed = binary ? parser.loadGltfBinary(data.get(), {}) : parser.loadGltfJson(data.get(), {});
        if (parsed.error() != fastgltf::Error::None) return ParserFailure(parsed.error());
        if (metadata.nodeCount != parsed.get().nodes.size() || metadata.materialCount != parsed.get().materials.size() ||
            metadata.textureCount != parsed.get().textures.size() || metadata.imageCount != parsed.get().images.size()) return Fail(ImportError::InvalidData);
        auto* storage = ImportAllocate(sizeof(ImportState));
        if (!storage) return Fail(ImportError::OutOfMemory);
        auto* candidate = new (storage) ImportState(std::move(parsed.get()));
        candidate->metadata.TakeFrom(metadata);
        if (auto result = Initialize(*candidate); !result) { Destroy(candidate); return result; }
        Reset();
        m_State = candidate;
        return {};
    }

    size_t ImportDocument::BufferCount() const noexcept { return m_State ? m_State->asset.buffers.size() : 0; }
    size_t ImportDocument::AccessorCount() const noexcept { return m_State ? m_State->asset.accessors.size() : 0; }
    ImportResult ImportDocument::BufferInfo(size_t index, ImportBufferInfo& output) const noexcept
    {
        if (index >= BufferCount()) return Fail(ImportError::InvalidIndex, ImportObject::Buffer, index);
        const auto& buffer = m_State->asset.buffers[index];
        ImportBufferInfo info;
        info.byteCount = buffer.byteLength;
        info.resident = m_State->buffers[index].bytes.data != nullptr;
        if (const auto* source = std::get_if<fastgltf::sources::URI>(&buffer.data))
        {
            const auto uri = source->uri.string();
            info.uri = {uri.data(), uri.size()};
        }
        output = info;
        return {};
    }
    ImportResult ImportDocument::SupplyBuffer(size_t index, ArrayView<const uint8_t> bytes) noexcept
    {
        if (index >= BufferCount()) return Fail(ImportError::InvalidIndex, ImportObject::Buffer, index);
        const auto& source = m_State->asset.buffers[index];
        if (!bytes.IsValid() || bytes.count < source.byteLength) return Fail(ImportError::InvalidRange, ImportObject::Buffer, index);
        if (!std::get_if<fastgltf::sources::URI>(&source.data)) return Fail(ImportError::UnsupportedData, ImportObject::Buffer, index);
        auto* copy = static_cast<uint8_t*>(ImportAllocate(source.byteLength));
        if (!copy) return Fail(ImportError::OutOfMemory, ImportObject::Buffer, index);
        memcpy(copy, bytes.data, source.byteLength);
        auto& target = m_State->buffers[index];
        free(target.owned);
        target.owned = copy;
        target.bytes = {copy, source.byteLength};
        return {};
    }
    ImportResult ImportDocument::AccessorInfo(size_t index, ImportAccessorInfo& output) const noexcept
    {
        if (index >= AccessorCount()) return Fail(ImportError::InvalidIndex, ImportObject::Accessor, index);
        ImportAccessorInfo info;
        if (auto result = Describe(m_State->asset.accessors[index], index, info); !result) return result;
        output = info;
        return {};
    }
    ImportResult ImportDocument::ReadFloats(size_t index, ArrayView<float> output) const noexcept { return Read(m_State, index, output, true); }
    ImportResult ImportDocument::ReadUnsigned(size_t index, ArrayView<uint32_t> output) const noexcept { return Read(m_State, index, output, false); }

    const char* ImportErrorText(ImportError error) noexcept
    {
        switch (error)
        {
        case ImportError::None: return "success";
        case ImportError::InvalidInput: return "invalid input bytes";
        case ImportError::InvalidJson: return "invalid JSON";
        case ImportError::InvalidContainer: return "invalid GLB container";
        case ImportError::UnsupportedVersion: return "unsupported glTF version";
        case ImportError::UnsupportedExtension: return "unsupported required glTF extension";
        case ImportError::InvalidData: return "invalid glTF data";
        case ImportError::InvalidUri: return "invalid asset URI";
        case ImportError::Overflow: return "import size overflow";
        case ImportError::OutOfMemory: return "import allocation failed";
        case ImportError::InvalidIndex: return "invalid import index";
        case ImportError::InvalidAccessor: return "invalid accessor type or count";
        case ImportError::InvalidRange: return "invalid buffer range or alignment";
        case ImportError::InvalidSparse: return "invalid sparse accessor";
        case ImportError::BufferUnavailable: return "buffer bytes have not been supplied";
        case ImportError::InvalidOutput: return "accessor output has the wrong size or alignment";
        case ImportError::NonFiniteValue: return "non-finite accessor value";
        case ImportError::UnsupportedData: return "unsupported import data source";
        case ImportError::InvalidState: return "invalid import owner state";
        case ImportError::InvalidHierarchy: return "invalid scene hierarchy";
        case ImportError::Cycle: return "cycle in scene hierarchy";
        case ImportError::Capacity: return "import capacity exceeded";
        case ImportError::Workspace: return "import workspace limit exceeded";
        case ImportError::FileUnavailable: return "asset file is unavailable";
        case ImportError::Io: return "asset file I/O failed";
        case ImportError::Canceled: return "scene loading was canceled";
        }
        return "unknown import error";
    }
#if defined(UVSR_BUILD_TESTING)
    void SetImportAllocationFailureCountdown(int64_t successfulAllocations) noexcept { allocationCountdown = successfulAllocations; }
#endif
}
