/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "renderer_import_description.h"
#include "renderer_import_path.h"
#include "import/renderer_import_allocation.h"
#include "import/renderer_import_description_private.h"

#include <simdjson.h>
#include <float.h>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>

namespace uvsr
{
    namespace
    {
        using Object = simdjson::dom::object;
        using Array = simdjson::dom::array;
        using Element = simdjson::dom::element;
        constexpr uint32_t invalid = InvalidSceneIndex;

        ImportResult Failure(ImportError error = ImportError::InvalidData, ImportObject object = ImportObject::Scene,
            size_t index = SIZE_MAX) noexcept { return {error, object, index}; }

        bool Add(uint32_t& count, size_t value) noexcept
        {
            if (value >= invalid || value > invalid - 1 - count) return false;
            count += uint32_t(value);
            return true;
        }

        bool Field(const Object& object, const char* key, Element& output, bool& present) noexcept
        {
            present = false;
            for (auto field : object)
                if (field.key == key)
                {
                    if (present) return false;
                    present = true;
                    output = field.value;
                }
            if (present && output.is_null()) present = false;
            return true;
        }

        bool ArrayField(const Object& object, const char* key, Array& output, bool& present) noexcept
        {
            Element item;
            return Field(object, key, item, present) && (!present || !item.get_array().get(output));
        }

        bool StringField(const Object& object, const char* key, std::string_view& output, bool& present) noexcept
        {
            Element item;
            return Field(object, key, item, present) && (!present || !item.get_string().get(output));
        }

        bool Number(const Object& object, const char* key, float& output, bool* hasValue = nullptr) noexcept
        {
            Element item;
            bool present = false;
            if (!Field(object, key, item, present)) return false;
            if (hasValue) *hasValue = present;
            if (!present) return true;
            double value = 0;
            if (item.get_double().get(value) || !isfinite(value) || value < -FLT_MAX || value > FLT_MAX) return false;
            output = float(value);
            return true;
        }

        bool Vector(const Object& object, const char* key, double* values, size_t count, bool& present) noexcept
        {
            Element item;
            if (!Field(object, key, item, present)) return false;
            if (!present) return true;
            double scalar = 0;
            if (!item.get_double().get(scalar))
            {
                if (!isfinite(scalar)) return false;
                for (size_t i = 0; i < count; ++i) values[i] = scalar;
                return true;
            }
            Array array;
            if (item.get_array().get(array) || array.size() != count) return false;
            size_t index = 0;
            for (auto value : array)
                if (value.get_double().get(values[index]) || !isfinite(values[index++])) return false;
            return true;
        }

        bool Color(const Object& object, gpu_contract::Float3& output) noexcept
        {
            double values[]{output.x, output.y, output.z};
            bool present = false;
            if (!Vector(object, "color", values, 3, present)) return false;
            for (double value : values) if (value < -FLT_MAX || value > FLT_MAX) return false;
            output = {float(values[0]), float(values[1]), float(values[2])};
            return true;
        }

        void QuaternionProduct(const double* a, const double* b, double* output) noexcept
        {
            output[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
            output[1] = a[3]*b[1] + a[1]*b[3] + a[2]*b[0] - a[0]*b[2];
            output[2] = a[3]*b[2] + a[2]*b[3] + a[0]*b[1] - a[1]*b[0];
            output[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
        }

        bool Transform(const Object& object, DescriptionNode& node) noexcept
        {
            bool present = false;
            if (!Vector(object, "translation", node.transform.translation, 3, present)) return false;
            if (present) node.transformFlags |= DescriptionTranslation;
            if (!Vector(object, "scaling", node.transform.scaling, 3, present)) return false;
            if (present) node.transformFlags |= DescriptionScaling;
            if (!Vector(object, "rotation", node.transform.rotation, 4, present)) return false;
            if (present) node.transformFlags |= DescriptionRotation;
            else
            {
                double euler[3]{};
                if (!Vector(object, "euler", euler, 3, present)) return false;
                if (present)
                {
                    const double x[]{sin(0.5*euler[0]), 0, 0, cos(0.5*euler[0])};
                    const double y[]{0, sin(0.5*euler[1]), 0, cos(0.5*euler[1])};
                    const double z[]{0, 0, sin(0.5*euler[2]), cos(0.5*euler[2])};
                    double zy[4]{};
                    QuaternionProduct(z, y, zy);
                    QuaternionProduct(zy, x, node.transform.rotation);
                    node.transformFlags |= DescriptionRotation;
                }
            }
            return true;
        }

        bool KeyVector(const Object& object, const char* key, gpu_contract::Float4& output) noexcept
        {
            Element item;
            bool present = false;
            if (!Field(object, key, item, present)) return false;
            if (!present) return true;
            double scalar = 0;
            if (!item.get_double().get(scalar))
            {
                if (!isfinite(scalar) || scalar < -FLT_MAX || scalar > FLT_MAX) return false;
                const float value = float(scalar);
                output = {value, value, value, value};
                return true;
            }
            Array array;
            if (item.get_array().get(array)) return false;
            float values[4]{};
            size_t index = 0;
            for (auto value : array)
            {
                if (index == 4) break;
                if (value.get_double().get(scalar) || !isfinite(scalar) || scalar < -FLT_MAX || scalar > FLT_MAX) return false;
                values[index++] = float(scalar);
            }
            output = {values[0], values[1], values[2], values[3]};
            return true;
        }

        struct Frame
        {
            Array::iterator next, end;
            uint32_t parent = invalid;
        };

        struct Frames
        {
            Frame* data = nullptr;
            size_t count = 0;
            ~Frames() noexcept { free(data); }
        };

        struct Reader
        {
            ImportDescriptionState* output = nullptr;
            DescriptionCounts counts;
            ArrayView<const char> fileName;
            Frames& frames;

            bool Text(std::string_view value, RendererSceneString& result) noexcept
            {
                if (value.size() >= invalid || (!value.empty() && memchr(value.data(), 0, value.size()))) return false;
                result = {counts.stringBytes, uint32_t(value.size())};
                if (!Add(counts.stringBytes, value.size() + 1)) return false;
                if (output)
                {
                    if (counts.stringBytes > output->counts.stringBytes) return false;
                    if (!value.empty()) memcpy(output->strings + result.offset, value.data(), value.size());
                    output->strings[result.offset + result.length] = 0;
                }
                return true;
            }

            bool Name(const Object& object, const char* key, RendererSceneString& result, bool* found = nullptr) noexcept
            {
                std::string_view value;
                bool present = false;
                if (!StringField(object, key, value, present)) return false;
                if (found) *found = present;
                return !present || Text(value, result);
            }

            ImportResult Models(const Object& root) noexcept
            {
                Array models;
                bool present = false;
                if (!ArrayField(root, "models", models, present)) return Failure();
                if (!present) return {};
                if (!Add(counts.models, models.size())) return Failure(ImportError::Capacity);
                uint32_t index = 0;
                for (auto item : models)
                {
                    std::string_view path;
                    if (item.get_string().get(path) || path.empty()) return Failure(ImportError::InvalidData, ImportObject::Document, index);
                    size_t length = 0;
                    const auto measured = MeasureImportPath(fileName, {path.data(), path.size()}, length);
                    if (!measured) return measured;
                    const uint32_t offset = counts.stringBytes;
                    if (!Add(counts.stringBytes, length + 1)) return Failure(ImportError::Capacity);
                    if (output)
                    {
                        if (counts.stringBytes > output->counts.stringBytes || index >= output->counts.models) return Failure(ImportError::InvalidState);
                        const auto resolved = ResolveImportPath(fileName, {path.data(), path.size()}, {output->strings + offset, length + 1}, length);
                        if (!resolved) return resolved;
                        output->models[index] = {offset, uint32_t(length)};
                    }
                    ++index;
                }
                return {};
            }

            bool Leaf(const Object& object, DescriptionNode& node, uint32_t nodeIndex) noexcept
            {
                std::string_view type;
                bool present = false;
                if (!StringField(object, "type", type, present)) return false;
                if (!present) return true;
                if (type == "DirectionalLight" || type == "PointLight" || type == "SpotLight")
                {
                    RendererSceneLight light;
                    light.nodeIndex = nodeIndex;
                    light.kind = type == "DirectionalLight" ? RendererSceneLightKind::Directional :
                        type == "PointLight" ? RendererSceneLightKind::Point : RendererSceneLightKind::Spot;
                    if (!Color(object, light.values.color)) return false;
                    if (light.kind == RendererSceneLightKind::Directional)
                    {
                        if (!Number(object, "irradiance", light.values.irradiance) || !Number(object, "angularSize", light.values.angularSize)) return false;
                    }
                    else if (!Number(object, "intensity", light.values.intensity) || !Number(object, "radius", light.values.radius) || !Number(object, "range", light.values.range)) return false;
                    if (light.kind == RendererSceneLightKind::Spot &&
                        (!Number(object, "innerAngle", light.values.innerAngle) || !Number(object, "outerAngle", light.values.outerAngle))) return false;
                    node.leafKind = RendererSceneLeafKind::Light;
                    node.leaf = counts.lights;
                    if (!Add(counts.lights, 1)) return false;
                    if (output) output->lights[node.leaf] = light;
                }
                else if (type == "PerspectiveCamera" || type == "OrthographicCamera")
                {
                    RendererSceneCamera camera;
                    camera.nodeIndex = nodeIndex;
                    camera.kind = type == "PerspectiveCamera" ? RendererSceneCameraKind::Perspective : RendererSceneCameraKind::Orthographic;
                    camera.nearPlane = camera.kind == RendererSceneCameraKind::Perspective ? 1.f : 0.f;
                    if (!Number(object, "zNear", camera.nearPlane)) return false;
                    if (camera.kind == RendererSceneCameraKind::Perspective)
                    {
                        if (!Number(object, "verticalFov", camera.verticalFov) || !Number(object, "zFar", camera.farPlane, &camera.hasFarPlane) ||
                            !Number(object, "aspectRatio", camera.aspectRatio, &camera.hasAspectRatio)) return false;
                    }
                    else if (!Number(object, "zFar", camera.farPlane) || !Number(object, "xMag", camera.xMagnitude) || !Number(object, "yMag", camera.yMagnitude)) return false;
                    node.leafKind = RendererSceneLeafKind::Camera;
                    node.leaf = counts.cameras;
                    if (!Add(counts.cameras, 1)) return false;
                    if (output) output->cameras[node.leaf] = camera;
                }
                else if (output) ++output->ignoredLeafTypes;
                return true;
            }

            ImportResult Graph(const Object& root) noexcept
            {
                Array graph;
                bool present = false;
                if (!ArrayField(root, "graph", graph, present)) return Failure();
                if (!present || graph.size() == 0) return {};
                size_t depth = 1;
                if (!frames.count) return Failure(ImportError::Workspace);
                frames.data[0] = {graph.begin(), graph.end(), invalid};
                while (depth)
                {
                    auto& frame = frames.data[depth - 1];
                    if (frame.next == frame.end)
                    {
                        if (output && frame.parent != invalid) output->nodes[frame.parent].subtreeEnd = counts.nodes;
                        --depth;
                        continue;
                    }
                    const Element item = *frame.next;
                    ++frame.next;
                    Object object;
                    const uint32_t index = counts.nodes;
                    if (item.get_object().get(object)) return Failure(ImportError::InvalidData, ImportObject::Node, index);
                    if (!Add(counts.nodes, 1)) return Failure(ImportError::Capacity, ImportObject::Node, index);
                    DescriptionNode node;
                    node.parent = frame.parent;
                    node.subtreeEnd = counts.nodes;
                    if (!Name(object, "name", node.name) || !Name(object, "parent", node.parentPath, &node.hasParent) || !Transform(object, node))
                        return Failure(ImportError::InvalidData, ImportObject::Node, index);
                    Element model;
                    if (!Field(object, "model", model, present)) return Failure(ImportError::InvalidData, ImportObject::Node, index);
                    if (present)
                    {
                        double value = 0;
                        if (model.get_double().get(value) || !isfinite(value) || value < 0 || value >= counts.models || floor(value) != value)
                            return Failure(ImportError::InvalidIndex, ImportObject::Node, index);
                        node.model = uint32_t(value);
                    }
                    if (!Leaf(object, node, index)) return Failure(ImportError::InvalidData, ImportObject::Node, index);
                    if (output) output->nodes[index] = node;
                    Array children;
                    if (!ArrayField(object, "children", children, present)) return Failure(ImportError::InvalidData, ImportObject::Node, index);
                    if (present && children.size())
                    {
                        if (depth == frames.count) return Failure(ImportError::Workspace, ImportObject::Node, index);
                        frames.data[depth++] = {children.begin(), children.end(), index};
                    }
                }
                return {};
            }

            bool Target(Element item) noexcept
            {
                std::string_view path;
                if (item.get_string().get(path)) return false;
                RendererSceneString text;
                if (!Text(path, text)) return false;
                const uint32_t index = counts.targets;
                if (!Add(counts.targets, 1)) return false;
                if (output) output->targets[index] = text;
                return true;
            }

            bool Channel(const Object& object, DescriptionChannel& channel) noexcept
            {
                std::string_view attribute, mode;
                bool present = false;
                if (!StringField(object, "attribute", attribute, present) || !present || attribute.empty() || !Text(attribute, channel.property)) return false;
                channel.attribute = attribute == "translation" ? RendererSceneAnimationAttribute::Translation :
                    attribute == "rotation" ? RendererSceneAnimationAttribute::Rotation :
                    attribute == "scaling" ? RendererSceneAnimationAttribute::Scaling : RendererSceneAnimationAttribute::LeafProperty;
                if (!StringField(object, "mode", mode, present)) return false;
                if (present)
                {
                    if (mode == "linear") channel.sampler.interpolation = RendererSceneInterpolation::Linear;
                    else if (mode == "slerp") channel.sampler.interpolation = RendererSceneInterpolation::Slerp;
                    else if (mode == "hermite") channel.sampler.interpolation = RendererSceneInterpolation::HermiteSpline;
                    else if (mode == "catmull-rom") channel.sampler.interpolation = RendererSceneInterpolation::CatmullRomSpline;
                    else if (mode != "step" && output) ++output->unknownInterpolationModes;
                }
                channel.sampler.keyframes.first = counts.keyframes;
                Array keys;
                if (!ArrayField(object, "data", keys, present)) return false;
                float previous = -FLT_MAX;
                if (present)
                    for (auto item : keys)
                    {
                        Object keyObject;
                        RendererSceneKeyframe key;
                        bool hasTime = false;
                        if (item.get_object().get(keyObject) || !Number(keyObject, "time", key.time, &hasTime) || !hasTime || key.time < previous ||
                            !KeyVector(keyObject, "value", key.value) || !KeyVector(keyObject, "inTangent", key.inTangent) || !KeyVector(keyObject, "outTangent", key.outTangent)) return false;
                        previous = key.time;
                        const uint32_t index = counts.keyframes;
                        if (!Add(counts.keyframes, 1)) return false;
                        if (output) output->keyframes[index] = key;
                    }
                channel.sampler.keyframes.count = counts.keyframes - channel.sampler.keyframes.first;
                channel.targets.first = counts.targets;
                Element target;
                if (!Field(object, "target", target, present)) return false;
                if (present) { if (!Target(target)) return false; }
                else
                {
                    Array targets;
                    if (!ArrayField(object, "targets", targets, present)) return false;
                    if (present) for (auto item : targets) if (!Target(item)) return false;
                }
                channel.targets.count = counts.targets - channel.targets.first;
                return true;
            }

            ImportResult Animations(const Object& root) noexcept
            {
                Array animations;
                bool present = false;
                if (!ArrayField(root, "animations", animations, present)) return Failure();
                if (!present) return {};
                for (auto item : animations)
                {
                    const uint32_t index = counts.animations;
                    Object object;
                    DescriptionAnimation animation;
                    if (item.get_object().get(object) || !Name(object, "name", animation.name)) return Failure(ImportError::InvalidData, ImportObject::Animation, index);
                    if (!Add(counts.animations, 1)) return Failure(ImportError::Capacity, ImportObject::Animation, index);
                    animation.channels.first = counts.channels;
                    Array channels;
                    if (!ArrayField(object, "channels", channels, present)) return Failure(ImportError::InvalidData, ImportObject::Animation, index);
                    if (present)
                        for (auto channelItem : channels)
                        {
                            DescriptionChannel channel;
                            Object channelObject;
                            if (channelItem.get_object().get(channelObject) || !Channel(channelObject, channel)) return Failure(ImportError::InvalidData, ImportObject::Animation, index);
                            const uint32_t channelIndex = counts.channels;
                            if (!Add(counts.channels, 1)) return Failure(ImportError::Capacity, ImportObject::Animation, index);
                            if (output) output->channels[channelIndex] = channel;
                        }
                    animation.channels.count = counts.channels - animation.channels.first;
                    if (output) output->animations[index] = animation;
                }
                return {};
            }

            ImportResult Read(const Object& root) noexcept
            {
                auto result = Models(root);
                if (result) result = Graph(root);
                if (result) result = Animations(root);
                return result;
            }
        };

        size_t JsonDepth(ArrayView<const uint8_t> bytes) noexcept
        {
            size_t depth = 0, maximum = 0;
            bool quoted = false;
            for (size_t index = 0; index < bytes.count; ++index)
            {
                const auto value = bytes.data[index];
                if (quoted && value == '\\') { ++index; continue; }
                if (value == '"') { quoted = !quoted; continue; }
                if (quoted) continue;
                if (value == '{' || value == '[') { ++depth; if (depth > maximum) maximum = depth; }
                else if (value == '}' || value == ']') --depth;
            }
            return maximum;
        }

        template<class T> bool Size(size_t count, size_t& bytes) noexcept
        {
            if (count > size_t(PTRDIFF_MAX) / sizeof(T) || count * sizeof(T) > SIZE_MAX - bytes) return false;
            bytes += count * sizeof(T);
            return true;
        }

        template<class T> bool Allocate(T*& output, size_t count) noexcept
        {
            if (!count) return true;
            output = static_cast<T*>(ImportAllocate(count * sizeof(T)));
            if (!output) return false;
            for (size_t i = 0; i < count; ++i) new (&output[i]) T{};
            return true;
        }

        void Destroy(ImportDescriptionState* state) noexcept
        { if (state) { state->~ImportDescriptionState(); free(state); } }

        struct Candidate
        {
            ImportDescriptionState* state = nullptr;
            ~Candidate() noexcept { Destroy(state); }
        };
    }

    ImportDescriptionState::~ImportDescriptionState() noexcept
    {
        free(models); free(nodes); free(lights); free(cameras); free(animations);
        free(channels); free(targets); free(keyframes); free(strings);
    }

    ImportSceneDescription::~ImportSceneDescription() noexcept { Reset(); }
    ImportSceneDescription::ImportSceneDescription(ImportSceneDescription&& other) noexcept : m_State(other.m_State)
    { other.m_State = nullptr; }
    ImportSceneDescription& ImportSceneDescription::operator=(ImportSceneDescription&& other) noexcept
    {
        if (this != &other) { Reset(); m_State = other.m_State; other.m_State = nullptr; }
        return *this;
    }
    void ImportSceneDescription::Reset() noexcept { Destroy(m_State); m_State = nullptr; }
    size_t ImportSceneDescription::ModelCount() const noexcept { return m_State ? m_State->counts.models : 0; }
    ArrayView<const char> ImportSceneDescription::ModelPath(size_t index) const noexcept
    {
        if (index >= ModelCount()) return {};
        const auto text = m_State->models[index];
        return {m_State->strings + text.offset, text.length};
    }
    size_t ImportSceneDescription::StorageBytes() const noexcept { return m_State ? m_State->storageBytes : 0; }
    size_t ImportSceneDescription::ScratchBytes() const noexcept { return m_State ? m_State->scratchBytes : 0; }

    ImportResult ImportSceneDescription::Parse(ArrayView<const uint8_t> bytes, ArrayView<const char> fileName,
        const ImportDescriptionLimits& limits) noexcept
    {
        if (!bytes.IsValid() || !bytes.count || !fileName.count) return Failure(ImportError::InvalidInput);
        if (bytes.count > simdjson::SIMDJSON_MAXSIZE_BYTES || bytes.count > SIZE_MAX - simdjson::SIMDJSON_PADDING)
            return Failure(ImportError::Overflow);
        size_t pathLength = 0;
        if (auto result = MeasureImportPath(fileName, {}, pathLength); !result) return result;
        simdjson::dom::parser parser;
        Object root;
        const auto error = parser.parse(bytes.data, bytes.count).get(root);
        if (error) return {ImportError::InvalidJson, ImportObject::Scene, SIZE_MAX, uint32_t(error)};
        Frames frames;
        frames.count = JsonDepth(bytes);
        size_t scratchBytes = 0;
        if (!Size<Frame>(frames.count, scratchBytes)) return Failure(ImportError::Overflow);
        if (scratchBytes > limits.maxScratchBytes) return Failure(ImportError::Workspace);
        if (!Allocate(frames.data, frames.count)) return Failure(ImportError::OutOfMemory);
        Reader plan{nullptr, {}, fileName, frames};
        if (auto result = plan.Read(root); !result) return result;
        const auto counts = plan.counts;
        size_t storageBytes = sizeof(ImportDescriptionState);
        if (!Size<RendererSceneString>(counts.models, storageBytes) || !Size<DescriptionNode>(counts.nodes, storageBytes) ||
            !Size<RendererSceneLight>(counts.lights, storageBytes) || !Size<RendererSceneCamera>(counts.cameras, storageBytes) ||
            !Size<DescriptionAnimation>(counts.animations, storageBytes) || !Size<DescriptionChannel>(counts.channels, storageBytes) ||
            !Size<RendererSceneString>(counts.targets, storageBytes) || !Size<RendererSceneKeyframe>(counts.keyframes, storageBytes) ||
            !Size<char>(counts.stringBytes, storageBytes)) return Failure(ImportError::Overflow);
        if (storageBytes > limits.maxStorageBytes) return Failure(ImportError::Capacity);
        Candidate candidate;
        if (!Allocate(candidate.state, 1)) return Failure(ImportError::OutOfMemory);
        auto& state = *candidate.state;
        state.counts = counts;
        state.storageBytes = storageBytes;
        state.scratchBytes = scratchBytes;
        if (!Allocate(state.models, counts.models) || !Allocate(state.nodes, counts.nodes) || !Allocate(state.lights, counts.lights) ||
            !Allocate(state.cameras, counts.cameras) || !Allocate(state.animations, counts.animations) || !Allocate(state.channels, counts.channels) ||
            !Allocate(state.targets, counts.targets) || !Allocate(state.keyframes, counts.keyframes) || !Allocate(state.strings, counts.stringBytes)) return Failure(ImportError::OutOfMemory);
        Reader writer{candidate.state, {}, fileName, frames};
        if (auto result = writer.Read(root); !result) return result;
        if (memcmp(&writer.counts, &counts, sizeof(counts)) != 0) return Failure(ImportError::InvalidState);
        Reset();
        m_State = candidate.state;
        candidate.state = nullptr;
        return {};
    }
}
