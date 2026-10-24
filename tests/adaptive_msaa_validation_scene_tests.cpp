#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <donut/core/json.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr float kEpsilon = 1e-5f;

    struct CgltfDeleter
    {
        void operator()(cgltf_data* data) const
        {
            cgltf_free(data);
        }
    };

    using CgltfPtr = std::unique_ptr<cgltf_data, CgltfDeleter>;

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool NearlyEqual(float left, float right, float epsilon = kEpsilon)
    {
        return std::abs(left - right) <= epsilon;
    }

    bool StartsWith(const char* value, std::string_view prefix)
    {
        if (value == nullptr)
            return false;
        const std::string_view view(value);
        return view.size() >= prefix.size() &&
            view.substr(0u, prefix.size()) == prefix;
    }

    const cgltf_node& FindNode(
        const cgltf_data& data,
        std::string_view name)
    {
        for (cgltf_size index = 0u; index < data.nodes_count; ++index)
        {
            const cgltf_node& node = data.nodes[index];
            if (node.name != nullptr &&
                std::string_view(node.name) == name)
                return node;
        }
        throw std::runtime_error("missing validation node: " +
            std::string(name));
    }

    const cgltf_mesh& FindMesh(
        const cgltf_data& data,
        std::string_view name)
    {
        for (cgltf_size index = 0u; index < data.meshes_count; ++index)
        {
            const cgltf_mesh& mesh = data.meshes[index];
            if (mesh.name != nullptr &&
                std::string_view(mesh.name) == name)
                return mesh;
        }
        throw std::runtime_error("missing validation mesh: " +
            std::string(name));
    }

    const cgltf_material& FindMaterial(
        const cgltf_data& data,
        std::string_view name)
    {
        for (cgltf_size index = 0u; index < data.materials_count; ++index)
        {
            const cgltf_material& material = data.materials[index];
            if (material.name != nullptr &&
                std::string_view(material.name) == name)
                return material;
        }
        throw std::runtime_error("missing validation material: " +
            std::string(name));
    }

    const cgltf_accessor* FindAttribute(
        const cgltf_primitive& primitive,
        cgltf_attribute_type type,
        cgltf_int semanticIndex = 0)
    {
        for (cgltf_size index = 0u;
            index < primitive.attributes_count;
            ++index)
        {
            const cgltf_attribute& attribute =
                primitive.attributes[index];
            if (attribute.type == type && attribute.index == semanticIndex)
                return attribute.data;
        }
        return nullptr;
    }

    std::array<float, 3> ReadVec3(
        const cgltf_accessor& accessor,
        cgltf_size index)
    {
        std::array<float, 3> value{};
        Require(cgltf_accessor_read_float(
                &accessor,
                index,
                value.data(),
                value.size()) != 0,
            "failed to read VEC3 accessor " +
                std::string(accessor.name ? accessor.name : "<unnamed>"));
        return value;
    }

    std::array<float, 3> Subtract(
        const std::array<float, 3>& left,
        const std::array<float, 3>& right)
    {
        return {
            left[0] - right[0],
            left[1] - right[1],
            left[2] - right[2]
        };
    }

    std::array<float, 3> Cross(
        const std::array<float, 3>& left,
        const std::array<float, 3>& right)
    {
        return {
            left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]
        };
    }

    float Length(const std::array<float, 3>& value)
    {
        return std::sqrt(
            value[0] * value[0] +
            value[1] * value[1] +
            value[2] * value[2]);
    }

    void ValidatePositionBounds(const cgltf_accessor& positions)
    {
        Require(positions.type == cgltf_type_vec3 &&
                positions.component_type ==
                    cgltf_component_type_r_32f &&
                positions.count >= 3u &&
                positions.has_min &&
                positions.has_max,
            "every POSITION accessor must be bounded FLOAT VEC3 data");

        std::array<float, 3> actualMin{
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity()
        };
        std::array<float, 3> actualMax{
            -std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()
        };

        for (cgltf_size index = 0u; index < positions.count; ++index)
        {
            const std::array<float, 3> value =
                ReadVec3(positions, index);
            for (size_t component = 0u; component < 3u; ++component)
            {
                Require(std::isfinite(value[component]),
                    "POSITION accessor contains a non-finite value");
                Require(value[component] >=
                            positions.min[component] - kEpsilon &&
                        value[component] <=
                            positions.max[component] + kEpsilon,
                    "POSITION value escapes its declared bounds");
                actualMin[component] =
                    std::min(actualMin[component], value[component]);
                actualMax[component] =
                    std::max(actualMax[component], value[component]);
            }
        }

        for (size_t component = 0u; component < 3u; ++component)
        {
            Require(NearlyEqual(
                        actualMin[component],
                        positions.min[component]) &&
                    NearlyEqual(
                        actualMax[component],
                        positions.max[component]),
                "declared POSITION bounds must be exact");
        }
    }

    void ValidateIndexedPrimitive(const cgltf_primitive& primitive)
    {
        Require(primitive.type == cgltf_primitive_type_triangles,
            "validation geometry must contain only triangle lists");
        Require(primitive.indices != nullptr &&
                primitive.indices->type == cgltf_type_scalar &&
                primitive.indices->component_type ==
                    cgltf_component_type_r_16u &&
                primitive.indices->count >= 3u &&
                primitive.indices->count % 3u == 0u,
            "every validation primitive must use UINT16 triangle indices");

        const cgltf_accessor* positions = FindAttribute(
            primitive,
            cgltf_attribute_type_position);
        const cgltf_accessor* normals = FindAttribute(
            primitive,
            cgltf_attribute_type_normal);
        const cgltf_accessor* tangents = FindAttribute(
            primitive,
            cgltf_attribute_type_tangent);
        const cgltf_accessor* texcoords = FindAttribute(
            primitive,
            cgltf_attribute_type_texcoord,
            0);
        Require(positions != nullptr &&
                normals != nullptr &&
                tangents != nullptr &&
                texcoords != nullptr,
            "every primitive needs POSITION, NORMAL, TANGENT, and TEXCOORD_0");
        Require(normals->type == cgltf_type_vec3 &&
                tangents->type == cgltf_type_vec4 &&
                texcoords->type == cgltf_type_vec2 &&
                normals->component_type ==
                    cgltf_component_type_r_32f &&
                tangents->component_type ==
                    cgltf_component_type_r_32f &&
                texcoords->component_type ==
                    cgltf_component_type_r_32f &&
                normals->count == positions->count &&
                tangents->count == positions->count &&
                texcoords->count == positions->count,
            "vertex reconstruction attributes must match POSITION count");
        ValidatePositionBounds(*positions);

        for (cgltf_size index = 0u;
            index < primitive.indices->count;
            ++index)
        {
            Require(cgltf_accessor_read_index(
                        primitive.indices,
                        index) < positions->count,
                "triangle index exceeds its POSITION accessor");
        }
    }

    Json::Value LoadDescriptor(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        Require(stream.good(), "failed to open scene descriptor");
        Json::CharReaderBuilder builder;
        builder["collectComments"] = false;
        Json::Value root;
        std::string errors;
        Require(Json::parseFromStream(
                builder,
                stream,
                &root,
                &errors) &&
                root.isObject(),
            "failed to parse scene descriptor: " + errors);
        return root;
    }

    void ValidateDescriptor(const std::filesystem::path& packageRoot)
    {
        const Json::Value descriptor = LoadDescriptor(
            packageRoot / "adaptive_msaa_validation.scene.json");
        Require(descriptor["displayName"].isString() &&
                descriptor["displayName"].asString() ==
                    "Adaptive MSAA Validation",
            "scene descriptor display name differs");
        Require(descriptor["models"].isArray() &&
                descriptor["models"].size() == 1u &&
                descriptor["models"][0u].isString() &&
                descriptor["models"][0u].asString() ==
                    "adaptive_msaa_validation.gltf",
            "scene descriptor must launch the inline validation glTF");
        Require(descriptor["graph"].isArray() &&
                descriptor["graph"].size() == 1u &&
                descriptor["graph"][0u].isObject() &&
                descriptor["graph"][0u]["name"].isString() &&
                descriptor["graph"][0u]["name"].asString() ==
                    "Adaptive MSAA Validation Geometry" &&
                descriptor["graph"][0u]["model"].isUInt() &&
                descriptor["graph"][0u]["model"].asUInt() == 0u,
            "scene descriptor graph must instantiate model zero");

        const Json::Value& camera = descriptor["initialCamera"];
        const std::array<float, 3> expectedPosition{ 0.f, 0.2f, 8.f };
        const std::array<float, 3> expectedDirection{ 0.f, 0.f, -1.f };
        const std::array<float, 3> expectedUp{ 0.f, 1.f, 0.f };
        Require(camera.isObject() &&
                camera["position"].isArray() &&
                camera["direction"].isArray() &&
                camera["up"].isArray() &&
                camera["position"].size() == 3u &&
                camera["direction"].size() == 3u &&
                camera["up"].size() == 3u,
            "scene descriptor must define an exact camera basis");
        for (Json::ArrayIndex index = 0u; index < 3u; ++index)
        {
            Require(camera["position"][index].isNumeric() &&
                    camera["direction"][index].isNumeric() &&
                    camera["up"][index].isNumeric() &&
                    NearlyEqual(
                        camera["position"][index].asFloat(),
                        expectedPosition[index]) &&
                    NearlyEqual(
                        camera["direction"][index].asFloat(),
                        expectedDirection[index]) &&
                    NearlyEqual(
                        camera["up"][index].asFloat(),
                        expectedUp[index]),
                "scene descriptor camera differs from the audited pose");
        }
        Require(camera["verticalFovDegrees"].isNumeric() &&
                NearlyEqual(
                    camera["verticalFovDegrees"].asFloat(),
                    42.f),
            "scene descriptor field of view differs");
    }

    void ValidateNamedCoverage(const cgltf_data& data)
    {
        constexpr std::array<std::string_view, 13> expectedNodes{{
            "CrossingTriangles",
            "SharedMaterialSubmeshes",
            "InstancedDiamondLeft",
            "InstancedDiamondRight",
            "AlphaMaskCutout",
            "DoubleSidedBackface",
            "TinyTriangle",
            "SteepGrazingTriangle",
            "SharpSpecular",
            "NormalMapped",
            "LocalizedEmissive",
            "SilhouetteSpikes",
            "AnimatedOccluder"
        }};
        for (std::string_view name : expectedNodes)
            static_cast<void>(FindNode(data, name));

        constexpr std::array<std::string_view, 12> expectedMaterials{{
            "CrossingWarm",
            "CrossingCool",
            "SharedMatte",
            "InstancedGold",
            "AlphaMaskCutout",
            "DoubleSidedTeal",
            "TinyAndGrazing",
            "SharpSpecular",
            "NormalMapped",
            "EmissiveDetail",
            "SilhouetteDark",
            "MotionMarker"
        }};
        for (std::string_view name : expectedMaterials)
            static_cast<void>(FindMaterial(data, name));

        const cgltf_mesh& crossing = FindMesh(data, "CrossingMesh");
        Require(crossing.primitives_count == 2u &&
                crossing.primitives[0u].material !=
                    crossing.primitives[1u].material,
            "crossing triangles need two independently owned surfaces");

        const cgltf_accessor* nearPositions = FindAttribute(
            crossing.primitives[0u],
            cgltf_attribute_type_position);
        const cgltf_accessor* farPositions = FindAttribute(
            crossing.primitives[1u],
            cgltf_attribute_type_position);
        Require(nearPositions != nullptr && farPositions != nullptr &&
                nearPositions->min[0] < farPositions->max[0] &&
                nearPositions->max[0] > farPositions->min[0] &&
                nearPositions->min[1] < farPositions->max[1] &&
                nearPositions->max[1] > farPositions->min[1] &&
                nearPositions->min[2] >
                    farPositions->max[2],
            "crossing triangles must overlap in projection at distinct depths");

        const cgltf_mesh& shared = FindMesh(
            data,
            "SharedSubmeshMesh");
        const cgltf_material& sharedMaterial = FindMaterial(
            data,
            "SharedMatte");
        Require(shared.primitives_count == 2u &&
                shared.primitives[0u].material == &sharedMaterial &&
                shared.primitives[1u].material == &sharedMaterial &&
                shared.primitives[0u].indices !=
                    shared.primitives[1u].indices,
            "distinct submeshes must share one material without sharing identity");

        const cgltf_mesh& instancedMesh = FindMesh(
            data,
            "InstancedDiamondMesh");
        cgltf_size instanceCount = 0u;
        for (cgltf_size index = 0u; index < data.nodes_count; ++index)
        {
            if (data.nodes[index].mesh == &instancedMesh)
                ++instanceCount;
        }
        Require(instanceCount == 2u,
            "the instancing case must reference one mesh from two nodes");

        const cgltf_material& alpha = FindMaterial(
            data,
            "AlphaMaskCutout");
        Require(alpha.alpha_mode == cgltf_alpha_mode_mask &&
                NearlyEqual(alpha.alpha_cutoff, 0.5f) &&
                alpha.pbr_metallic_roughness.base_color_texture.texture !=
                    nullptr &&
                alpha.pbr_metallic_roughness.base_color_texture.texture
                    ->image != nullptr,
            "alpha-cutout material must use an exact MASK texture");

        const cgltf_material& doubleSided = FindMaterial(
            data,
            "DoubleSidedTeal");
        const cgltf_node& backface = FindNode(
            data,
            "DoubleSidedBackface");
        Require(doubleSided.double_sided &&
                backface.has_rotation &&
                NearlyEqual(std::abs(backface.rotation[1]), 1.f) &&
                NearlyEqual(backface.rotation[3], 0.f),
            "double-sided case must present the authored back face");

        const cgltf_material& sharpSpecular = FindMaterial(
            data,
            "SharpSpecular");
        Require(sharpSpecular.has_pbr_metallic_roughness &&
                sharpSpecular.pbr_metallic_roughness.metallic_factor >=
                    0.99f &&
                sharpSpecular.pbr_metallic_roughness.roughness_factor <=
                    0.02f,
            "sharp-specular material must remain metallic and low roughness");

        const cgltf_material& normalMapped = FindMaterial(
            data,
            "NormalMapped");
        Require(normalMapped.normal_texture.texture != nullptr &&
                normalMapped.normal_texture.texture->image != nullptr &&
                NearlyEqual(normalMapped.normal_texture.scale, 1.f),
            "normal-mapped material must reference its inline normal texture");

        const cgltf_material& emissive = FindMaterial(
            data,
            "EmissiveDetail");
        Require(emissive.emissive_texture.texture != nullptr &&
                emissive.emissive_texture.texture->image != nullptr &&
                emissive.emissive_factor[0] > 0.f &&
                emissive.emissive_factor[1] > 0.f &&
                emissive.emissive_factor[2] > 0.f,
            "emissive-detail material must retain localized texture energy");

        const cgltf_mesh& tiny = FindMesh(data, "TinyTriangleMesh");
        const cgltf_accessor* tinyPositions = FindAttribute(
            tiny.primitives[0u],
            cgltf_attribute_type_position);
        Require(tinyPositions != nullptr &&
                tinyPositions->max[0] - tinyPositions->min[0] <= 0.1f &&
                tinyPositions->max[1] - tinyPositions->min[1] <= 0.1f,
            "tiny geometry must stay small enough to stress sample coverage");

        const cgltf_mesh& grazing = FindMesh(
            data,
            "GrazingTriangleMesh");
        const cgltf_accessor* grazingPositions = FindAttribute(
            grazing.primitives[0u],
            cgltf_attribute_type_position);
        Require(grazingPositions != nullptr &&
                grazingPositions->count == 3u,
            "grazing case must remain one triangle");
        const std::array<float, 3> grazingNormal = Cross(
            Subtract(
                ReadVec3(*grazingPositions, 1u),
                ReadVec3(*grazingPositions, 0u)),
            Subtract(
                ReadVec3(*grazingPositions, 2u),
                ReadVec3(*grazingPositions, 0u)));
        Require(Length(grazingNormal) > 0.f &&
                std::abs(grazingNormal[2] / Length(grazingNormal)) < 0.2f,
            "grazing triangle must have a near-edge-on geometric normal");

        const cgltf_mesh& silhouettes = FindMesh(
            data,
            "SilhouetteMesh");
        Require(silhouettes.primitives_count == 2u,
            "silhouette case must retain two independently indexed spikes");
    }

    void ValidateAnimation(const cgltf_data& data)
    {
        Require(data.animations_count == 1u,
            "validation scene must contain one deterministic animation");
        const cgltf_animation& animation = data.animations[0u];
        Require(animation.name != nullptr &&
                std::string_view(animation.name) == "MotionTaaSweep" &&
                animation.samplers_count == 1u &&
                animation.channels_count == 1u,
            "motion/TAA animation contract differs");

        const cgltf_animation_sampler& sampler =
            animation.samplers[0u];
        const cgltf_animation_channel& channel =
            animation.channels[0u];
        const cgltf_node& animatedNode = FindNode(
            data,
            "AnimatedOccluder");
        Require(channel.target_node == &animatedNode &&
                channel.target_path ==
                    cgltf_animation_path_type_translation &&
                channel.sampler == &sampler &&
                sampler.interpolation ==
                    cgltf_interpolation_type_linear &&
                sampler.input != nullptr &&
                sampler.output != nullptr &&
                sampler.input->count == 3u &&
                sampler.output->count == 3u &&
                sampler.input->has_min &&
                sampler.input->has_max &&
                NearlyEqual(sampler.input->min[0], 0.f) &&
                NearlyEqual(sampler.input->max[0], 2.f),
            "animation must be a two-second LINEAR translation sweep");

        float minimumX = std::numeric_limits<float>::infinity();
        float maximumX = -std::numeric_limits<float>::infinity();
        for (cgltf_size index = 0u; index < sampler.output->count; ++index)
        {
            const std::array<float, 3> translation =
                ReadVec3(*sampler.output, index);
            minimumX = std::min(minimumX, translation[0]);
            maximumX = std::max(maximumX, translation[0]);
            Require(NearlyEqual(translation[1], -0.55f) &&
                    NearlyEqual(translation[2], 0.55f),
                "animated edge may translate only on the X axis");
        }
        Require(minimumX <= -1.45f &&
                maximumX >= 1.45f &&
                NearlyEqual(
                    ReadVec3(*sampler.output, 0u)[0],
                    ReadVec3(*sampler.output, 2u)[0]),
            "animated edge must sweep left-to-right and return exactly");
    }

    void ValidateImageDiffMatrix(
        const std::filesystem::path& packageRoot,
        const cgltf_data& data)
    {
        const Json::Value matrix = LoadDescriptor(
            packageRoot / "image_diff_matrix.json");
        Require(matrix["schema"].isUInt() &&
                matrix["schema"].asUInt() == 1u &&
                matrix["suiteId"].isString() &&
                matrix["suiteId"].asString() ==
                    "uvsr-adaptive-msaa-image-diff-v1" &&
                matrix["width"].isUInt() &&
                matrix["width"].asUInt() == 1920u &&
                matrix["height"].isUInt() &&
                matrix["height"].asUInt() == 1080u,
            "image-diff matrix identity or fixed resolution differs");

        const Json::Value& phases = matrix["phases"];
        Require(phases.isArray() && phases.size() == 2u &&
                phases[0u].isNumeric() && phases[1u].isNumeric() &&
                NearlyEqual(phases[0u].asFloat(), 0.25f) &&
                NearlyEqual(phases[1u].asFloat(), 0.5f),
            "image-diff matrix must retain the two consecutive validation phases");

        const Json::Value& camera = matrix["camera"];
        const std::array<float, 3> expectedPosition{ 0.f, 0.2f, 8.f };
        const std::array<float, 3> expectedDirection{ 0.f, 0.f, -1.f };
        const std::array<float, 3> expectedUp{ 0.f, 1.f, 0.f };
        for (const auto& expected : {
                std::pair<const char*, const std::array<float, 3>*>(
                    "position", &expectedPosition),
                std::pair<const char*, const std::array<float, 3>*>(
                    "direction", &expectedDirection),
                std::pair<const char*, const std::array<float, 3>*>(
                    "up", &expectedUp) })
        {
            const Json::Value& value = camera[expected.first];
            Require(value.isArray() && value.size() == 3u,
                "image-diff camera vector is missing");
            for (Json::ArrayIndex component = 0u;
                component < 3u;
                ++component)
            {
                Require(value[component].isNumeric() &&
                        NearlyEqual(
                            value[component].asFloat(),
                            (*expected.second)[component]),
                    "image-diff camera differs from the scene descriptor");
            }
        }
        Require(camera["verticalFovDegrees"].isNumeric() &&
                NearlyEqual(
                    camera["verticalFovDegrees"].asFloat(),
                    42.f),
            "image-diff camera FOV differs from the scene descriptor");

        constexpr std::array<std::string_view, 15u> RequiredCases{{
            "crossing-triangles",
            "shared-material-submeshes",
            "instancing",
            "alpha-cutouts",
            "double-sided",
            "tiny-steep-geometry",
            "sharp-specular",
            "normal-mapped",
            "emissive-detail",
            "sky-edges",
            "motion-taa",
            "moving-shadow-edges",
            "nearby-lights",
            "grazing-geometry",
            "screen-space-visibility"
        }};
        const Json::Value& cases = matrix["cases"];
        Require(cases.isArray() && cases.size() == RequiredCases.size(),
            "image-diff matrix must contain all fifteen cases exactly once");
        std::set<std::string> observedCases;
        for (const Json::Value& entry : cases)
        {
            Require(entry.isObject() && entry["id"].isString(),
                "image-diff case identity is missing");
            const std::string id = entry["id"].asString();
            Require(std::find(
                        RequiredCases.begin(),
                        RequiredCases.end(),
                        id) != RequiredCases.end() &&
                    observedCases.insert(id).second,
                "image-diff case is unknown or duplicated");

            const Json::Value& roi = entry["roi"];
            Require(roi.isObject() &&
                    roi["x"].isUInt() && roi["y"].isUInt() &&
                    roi["width"].isUInt() && roi["height"].isUInt() &&
                    roi["width"].asUInt() != 0u &&
                    roi["height"].asUInt() != 0u &&
                    uint64_t(roi["x"].asUInt()) +
                            roi["width"].asUInt() <= 1920u &&
                    uint64_t(roi["y"].asUInt()) +
                            roi["height"].asUInt() <= 1080u,
                "image-diff case ROI escapes the fixed capture extent");

            const Json::Value& nodes = entry["nodes"];
            const Json::Value& materials = entry["materials"];
            Require(nodes.isArray() && !nodes.empty() &&
                    materials.isArray() && !materials.empty(),
                "image-diff case needs explicit scene targets");
            for (const Json::Value& node : nodes)
            {
                Require(node.isString(),
                    "image-diff node target must be a string");
                static_cast<void>(FindNode(data, node.asString()));
            }
            for (const Json::Value& material : materials)
            {
                Require(material.isString(),
                    "image-diff material target must be a string");
                static_cast<void>(FindMaterial(
                    data,
                    material.asString()));
            }

            const Json::Value& settings = entry["settings"];
            for (const char* setting : {
                    "taa",
                    "flashlight",
                    "flashlightShadows",
                    "directionalShadows",
                    "screenSpaceVisibility" })
            {
                Require(settings[setting].isBool(),
                    "image-diff case setting must be an explicit boolean");
            }
        }
    }

    void ValidateDataUris(const cgltf_data& data)
    {
        Require(data.buffers_count == 1u &&
                StartsWith(
                    data.buffers[0u].uri,
                    "data:application/octet-stream;base64,") &&
                data.buffers[0u].data != nullptr,
            "geometry and animation buffer must be an inline data URI");
        Require(data.images_count == 3u &&
                data.textures_count == 3u,
            "validation scene must contain exactly three inline textures");
        for (cgltf_size index = 0u; index < data.images_count; ++index)
        {
            Require(StartsWith(
                        data.images[index].uri,
                        "data:image/png;base64,") &&
                    data.images[index].mime_type != nullptr &&
                    std::string_view(data.images[index].mime_type) ==
                        "image/png",
                "validation image must be an inline PNG data URI");
        }
    }

    void ValidateScene(const std::filesystem::path& packageRoot)
    {
        const std::filesystem::path gltfPath =
            packageRoot / "adaptive_msaa_validation.gltf";
        const std::string nativePath = gltfPath.string();
        cgltf_options options{};
        cgltf_data* rawData = nullptr;
        Require(cgltf_parse_file(
                    &options,
                    nativePath.c_str(),
                    &rawData) == cgltf_result_success &&
                rawData != nullptr,
            "cgltf failed to parse the validation scene");
        CgltfPtr data(rawData);
        Require(cgltf_load_buffers(
                    &options,
                    data.get(),
                    nativePath.c_str()) == cgltf_result_success,
            "cgltf failed to load the inline validation buffer");
        Require(cgltf_validate(data.get()) == cgltf_result_success,
            "cgltf rejected the validation scene");

        Require(data->file_type == cgltf_file_type_gltf &&
                data->scene != nullptr &&
                data->scene->nodes_count == 13u &&
                data->asset.generator != nullptr &&
                std::string_view(data->asset.generator) ==
                    "UVSR Adaptive MSAA Validation Scene Generator 1.0" &&
                data->asset.copyright != nullptr &&
                std::string_view(data->asset.copyright).find(
                    "MIT License") != std::string_view::npos,
            "validation scene identity or default scene differs");

        for (cgltf_size meshIndex = 0u;
            meshIndex < data->meshes_count;
            ++meshIndex)
        {
            const cgltf_mesh& mesh = data->meshes[meshIndex];
            Require(mesh.primitives_count > 0u,
                "every validation mesh must contain geometry");
            for (cgltf_size primitiveIndex = 0u;
                primitiveIndex < mesh.primitives_count;
                ++primitiveIndex)
            {
                ValidateIndexedPrimitive(
                    mesh.primitives[primitiveIndex]);
            }
        }

        ValidateDataUris(*data);
        ValidateNamedCoverage(*data);
        ValidateAnimation(*data);
        ValidateImageDiffMatrix(packageRoot, *data);
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2,
            "usage: adaptive_msaa_validation_scene_tests <package-root>");
        const std::filesystem::path packageRoot =
            std::filesystem::path(argv[1]).lexically_normal();
        Require(std::filesystem::is_regular_file(
                    packageRoot / "LICENSE.txt") &&
                std::filesystem::is_regular_file(
                    packageRoot / "SOURCE-README.md"),
            "validation package must retain license and provenance files");
        ValidateDescriptor(packageRoot);
        ValidateScene(packageRoot);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Adaptive MSAA validation scene test failed: "
            << error.what() << '\n';
        return 1;
    }

    std::cout << "Adaptive MSAA validation scene tests passed\n";
    return 0;
}
