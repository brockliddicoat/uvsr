#include "json_document.h"
#include "scene_catalog.h"
#include "sha256.h"

#include <DirectXCollision.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

void CheckLightingAssets(const std::filesystem::path& environmentRoot, const std::filesystem::path& noiseRoot);

namespace
{
    namespace fs = std::filesystem;
    using namespace DirectX;
    using Json = uvsr::json::Value;
    using Kind = Json::Kind;

    void Require(bool value, std::string_view message)
    {
        if (!value)
            throw std::runtime_error(std::string(message));
    }

    const Json& Member(const Json& value, std::string_view name, Kind kind)
    {
        const auto* member = value.Find(name);
        Require(member && member->kind == kind, "missing or mistyped JSON field: " + std::string(name));
        return *member;
    }

    uint64_t Integer(const Json& value, std::string_view name)
    {
        const double number = Member(value, name, Kind::Number).number;
        Require(number >= 0 && number <= 1e12 && std::floor(number) == number, "invalid unsigned audit value");
        return static_cast<uint64_t>(number);
    }

    std::string Lower(std::string value)
    {
        for (char& c : value)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }

    std::string Relative(std::string value)
    {
        const fs::path path(value);
        Require(!value.empty() && value.find_first_of(":\\;\r\n?#") == std::string::npos &&
            value.find('\0') == std::string::npos && !path.is_absolute() && !path.has_root_path() &&
            path.lexically_normal().generic_string() == value, "unsafe relative asset path: " + value);
        for (const auto& part : path)
            Require(part != "." && part != ".." && !part.empty(), "asset path escapes its root");
        return value;
    }

    std::set<std::string> Files(const fs::path& root, bool staged = false)
    {
        Require(fs::is_directory(root) && !fs::is_symlink(root), "missing or linked asset root: " + root.string());
        std::set<std::string> files, caseKeys;
        for (const auto& item : fs::recursive_directory_iterator(root))
        {
            Require(!item.is_symlink(), "asset tree contains a symlink: " + item.path().string());
            if (item.is_directory())
                continue;
            Require(item.is_regular_file(), "asset is not a regular file");
            const auto relative = Relative(item.path().lexically_relative(root).generic_string());
            if (item.path().filename() == ".uvsr-stage.stamp")
            {
                Require(staged, "source tree contains a generated staging stamp");
                continue;
            }
            Require(item.file_size() < 100000000u, "asset exceeds the strict 100 MB tracked-file limit");
            Require(caseKeys.insert(Lower(relative)).second, "asset paths collide on Windows");
            files.insert(relative);
        }
        Require(!files.empty(), "asset inventory is empty");
        return files;
    }

    struct Scene
    {
        const char* directory;
        const char* label;
        const char* model;
        uvsr::SceneInitialCamera camera;
        const char* provenanceHash;
        const char* repackHash;
        size_t materialCount, imageCount;
        bool bistro;
    };

    constexpr Scene Scenes[] = {
        { "bistro_interior_retextured", "Bistro Interior", "bistro_interior.gltf",
            { { 4.444546f, 2.258351f, -2.746721f }, { .992681f, -.037313f, -.114857f },
                { .037065f, .999304f, -.004289f }, 33.9666f },
            "5acbcd2585a9c3be6d04715ccfb9b5ed7018c3a9b73bf3d9c0662990117511bf",
            "613b86564d7f785a43b83f9ba59b008a758d3f8e0dac93a2e7e9b0d154f31a76", 74, 201, true },
        { "san_miguel_retextured", "San Miguel", "san_miguel.gltf",
            { { 27.6255f, 1.49616f, 2.42353f }, { -.9673232088f, -.0081301951f, -.2534160802f },
                { -.0078644596f, .9999669523f, -.0020602299f }, 57.2209f },
            "1117561844626cea69078c3668cf389de8ef5dd4c97ce793427474f37dbf7577",
            "2fdb20e180585fffe00e0bff799ed825044a8262fdc08cd3a9e5aab7cb096cdc", 287, 269, false }
    };

    Json Audit(const fs::path& path, std::string_view hash)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(bool(stream), "missing historical audit: " + path.string());
        std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        Require(!stream.bad(), "cannot read historical audit");
        // Checkout line endings vary. All other bytes of these reviewed records are immutable.
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        Require(uvsr::Sha256(text) == hash, "historical audit changed: " + path.string());
        return uvsr::json::Parse(text);
    }

    void CheckLegal(const fs::path& sourceRoot)
    {
        constexpr std::pair<const char*, const char*> legal[] = {
            { "bistro_interior_retextured/LICENSE.txt",
                "9a9ef3c33320eebe6126b0c7dc327885806bde242283bbe6e4ae77641ad703e4" },
            { "bistro_interior_retextured/SOURCE-README.txt",
                "c87c5b60992cedee49fce1ea9bfe10cf60498ebf1113685b723d6dc9006c2bef" },
            { "san_miguel_retextured/LICENSE.txt",
                "708c9ad36adac62d13bd61ddf47d58d2b892b9e318bd87da93ae9e55e2b5e680" }
        };
        for (const auto& [path, hash] : legal)
            Require(uvsr::Sha256File(sourceRoot / path) == hash, "bundled source attribution or license changed");
        const auto repository = sourceRoot.parent_path().parent_path();
        Require(!fs::exists(repository / "tools/repack_gltf_buffers.py") &&
            !fs::exists(repository / "tools/import_san_miguel.py"), "retired conversion tool was restored");
    }

    void CheckStaging(const fs::path& sourceRoot, const fs::path& stagedRoot, const fs::path& mapPath)
    {
        std::ifstream mapFile(mapPath);
        Require(bool(mapFile), "runtime asset map is missing");
        constexpr std::string_view prefix = "media/glTF-Sample-Assets/Models/";
        std::map<std::string, std::string> mappings;
        std::set<std::string> expected;
        for (std::string row; std::getline(mapFile, row);)
        {
            if (!row.empty() && row.back() == '\r')
                row.pop_back();
            const auto separator = row.find('|');
            Require(separator != std::string::npos && row.find('|', separator + 1) == std::string::npos,
                "malformed runtime asset map");
            const auto package = Relative(row.substr(0, separator));
            const auto source = Relative(row.substr(separator + 1));
            Require(mappings.emplace(package, source).second, "duplicate runtime asset mapping");
            if (package.compare(0, prefix.size(), prefix) != 0)
                continue;
            const auto path = Relative(package.substr(prefix.size()));
            const auto scene = *fs::path(path).begin();
            Require(scene == Scenes[0].directory || scene == Scenes[1].directory, "unexpected runtime scene");
            Require(source == "assets/scenes/" + path, "scene mapping changes the canonical source identity");
            expected.insert(path);
        }
        Require(!mapFile.bad() && !expected.empty(), "runtime scene map is empty or unreadable");
        Require(Files(stagedRoot, true) == expected, "staged scene inventory differs from the runtime map");
        for (const auto& relative : expected)
            Require(uvsr::Sha256File(sourceRoot / relative) == uvsr::Sha256File(stagedRoot / relative),
                "staged asset bytes differ from source: " + relative);
        Require(mappings["bin/licenses/Amazon-Lumberyard-Bistro.txt"] ==
                "assets/scenes/bistro_interior_retextured/LICENSE.txt" &&
            mappings["bin/licenses/San-Miguel-2.1.txt"] == "assets/scenes/san_miguel_retextured/LICENSE.txt",
            "runtime package omits a retained scene license");
    }

    Json CheckRepack(const fs::path& root, const Scene& scene)
    {
        const auto provenance = Audit(root / "source-provenance.json", scene.provenanceHash);
        Require(Member(provenance, "scene", Kind::String).string == scene.directory, "provenance scene mismatch");
        const auto report = Audit(root / "components/buffer-repack-report.json", scene.repackHash);
        const auto& outputs = Member(report, "files", Kind::Array).array;
        std::set<std::string> reported;
        uint64_t bufferBytes = 0;
        size_t buffers = 0;
        for (const auto& output : outputs)
        {
            const auto path = Relative(Member(output, "path", Kind::String).string);
            const auto bytes = Integer(output, "bytes");
            Require(reported.insert(path).second, "repack lists an output twice");
            Require(fs::file_size(root / "components" / path) == bytes &&
                uvsr::Sha256File(root / "components" / path) ==
                    Lower(Member(output, "sha256", Kind::String).string),
                "repacked output differs from its audited bytes: " + path);
            if (fs::path(path).extension() == ".bin")
            {
                Require(bytes <= 90000000u, "repacked buffer exceeds its 90 MB limit");
                bufferBytes += bytes;
                ++buffers;
            }
        }
        auto actual = Files(root / "components");
        actual.erase("buffer-repack-report.json");
        actual.erase("blender-export-report.json");
        Require(actual == reported && reported.count(scene.model) == 1, "repack inventory is incomplete");
        Require(buffers == 5 && bufferBytes ==
            Integer(report, "copiedBufferViewBytes") + Integer(report, "alignmentPaddingBytes"),
            "repack buffer count or lossless byte accounting changed");
        if (!scene.bistro)
        {
            const auto imported = Audit(root / "blender-import-report.json",
                "21694870a8584b0854b96737b3955dc49ffe814991633a5d82798bb214911a3b");
            Require(Member(imported, "outputGltfSha256", Kind::String).string ==
                Member(report, "sourceContainerSha256", Kind::String).string, "Blender/repack chain is broken");
        }
        return report;
    }

    XMVECTOR Vector(const std::array<float, 3>& value)
    {
        return XMVectorSet(value[0], value[1], value[2], 0);
    }

    std::vector<const cgltf_node*> SceneNodes(const cgltf_data& data)
    {
        std::vector<const cgltf_node*> nodes;
        std::set<const cgltf_node*> visited;
        std::function<void(const cgltf_node*)> visit = [&](const cgltf_node* node) {
            Require(node && visited.insert(node).second, "default scene repeats a node or contains a cycle");
            nodes.push_back(node);
            for (size_t i = 0; i < node->children_count; ++i)
            {
                Require(node->children[i]->parent == node, "default scene has an inconsistent parent");
                visit(node->children[i]);
            }
        };
        for (size_t i = 0; i < data.scene->nodes_count; ++i)
        {
            Require(!data.scene->nodes[i]->parent, "default scene root has a parent");
            visit(data.scene->nodes[i]);
        }
        return nodes;
    }

    void CheckGeometry(const cgltf_data& data, const Scene& scene)
    {
        struct Primitive
        {
            const cgltf_primitive* primitive;
            const cgltf_accessor* positions;
            XMFLOAT4X4 world;
        };
        std::vector<Primitive> primitives;
        BoundingBox bounds;
        bool hasBounds = false, transformedMesh = false, embeddedCamera = false;
        const auto camera = Vector(scene.camera.Position);
        const auto forward = XMVector3Normalize(Vector(scene.camera.Direction));
        const auto up = XMVector3Normalize(Vector(scene.camera.Up));
        for (const auto* node : SceneNodes(data))
        {
            XMFLOAT4X4 world;
            cgltf_node_transform_world(node, &world._11);
            const auto matrix = XMLoadFloat4x4(&world);
            Require(!XMMatrixIsNaN(matrix) && !XMMatrixIsInfinite(matrix), "nonfinite node transform");
            if (node->camera && node->camera->type == cgltf_camera_type_perspective)
            {
                const auto offset = XMVectorSet(1, 0, -.5f, 0);
                embeddedCamera |= XMVector3NearEqual(
                        XMVector3TransformCoord(XMVectorZero(), matrix), XMVectorSubtract(camera, offset),
                        XMVectorReplicate(1e-4f)) &&
                    XMVectorGetX(XMVector3Dot(XMVector3Normalize(XMVector3TransformNormal(
                        XMVectorSet(0, 0, -1, 0), matrix)), forward)) >= .999999f &&
                    XMVectorGetX(XMVector3Dot(XMVector3Normalize(XMVector3TransformNormal(
                        XMVectorSet(0, 1, 0, 0), matrix)), up)) >= .999999f &&
                    std::abs(XMConvertToDegrees(node->camera->data.perspective.yfov) -
                        scene.camera.VerticalFovDegrees) <= 1e-3f;
            }
            if (!node->mesh)
                continue;
            transformedMesh |= !XMMatrixIsIdentity(matrix);
            for (size_t i = 0; i < node->mesh->primitives_count; ++i)
            {
                const auto& primitive = node->mesh->primitives[i];
                const cgltf_accessor* position = nullptr;
                for (size_t j = 0; j < primitive.attributes_count; ++j)
                    if (primitive.attributes[j].type == cgltf_attribute_type_position)
                    {
                        Require(!position && primitive.attributes[j].index == 0, "duplicate POSITION accessor");
                        position = primitive.attributes[j].data;
                    }
                Require(position && position->type == cgltf_type_vec3 && position->count > 0 &&
                    position->has_min && position->has_max && primitive.type == cgltf_primitive_type_triangles,
                    "scene primitive has no bounded triangle positions");
                BoundingBox local, transformed;
                BoundingBox::CreateFromPoints(local,
                    XMVectorSet(position->min[0], position->min[1], position->min[2], 0),
                    XMVectorSet(position->max[0], position->max[1], position->max[2], 0));
                local.Transform(transformed, matrix);
                if (hasBounds)
                    BoundingBox::CreateMerged(bounds, bounds, transformed);
                else
                    bounds = transformed;
                hasBounds = true;
                primitives.push_back({ &primitive, position, world });
            }
        }
        Require(hasBounds && bounds.Contains(camera) == CONTAINS, "initial camera is outside scene bounds");
        Require(!scene.bistro || (transformedMesh && embeddedCamera),
            "Bistro lost transformed meshes or its audited embedded camera relation");
        const float diagonal = 2.f * XMVectorGetX(XMVector3Length(XMLoadFloat3(&bounds.Extents)));
        const float radius = std::max(.1f, diagonal * .0005f);
        XMFLOAT3 center;
        XMStoreFloat3(&center, camera);
        const BoundingSphere clearance(center, radius + .01f);
        const XMVECTOR directions[] = { XMVectorSet(0, -1, 0, 0), XMVectorSet(1, 0, 0, 0),
            XMVectorSet(-1, 0, 0, 0), XMVectorSet(0, 0, 1, 0), XMVectorSet(0, 0, -1, 0), forward };
        std::array<float, 6> hits;
        hits.fill(std::numeric_limits<float>::infinity());
        uint64_t triangles = 0;
        for (const auto& entry : primitives)
        {
            const auto* indices = entry.primitive->indices;
            const size_t count = indices ? indices->count : entry.positions->count;
            Require(count > 0 && count % 3 == 0, "triangle index count is invalid");
            const auto matrix = XMLoadFloat4x4(&entry.world);
            const auto vertex = [&](size_t element) {
                const size_t index = indices ? cgltf_accessor_read_index(indices, element) : element;
                XMFLOAT3 position;
                Require(index < entry.positions->count &&
                    cgltf_accessor_read_float(entry.positions, index, &position.x, 3), "invalid triangle vertex");
                const auto result = XMVector3TransformCoord(XMLoadFloat3(&position), matrix);
                Require(!XMVector3IsNaN(result) && !XMVector3IsInfinite(result), "nonfinite triangle vertex");
                return result;
            };
            for (size_t i = 0; i < count; i += 3)
            {
                const auto a = vertex(i), b = vertex(i + 1), c = vertex(i + 2);
                ++triangles;
                // DirectXCollision requires nondegenerate triangles, as does the production collision world.
                if (XMVectorGetX(XMVector3LengthSq(XMVector3Cross(
                        XMVectorSubtract(b, a), XMVectorSubtract(c, a)))) <= 1e-20f)
                    continue;
                Require(!clearance.Intersects(a, b, c), "initial camera sphere intersects authored geometry");
                for (size_t ray = 0; ray < hits.size(); ++ray)
                {
                    float distance = 0;
                    if (TriangleTests::Intersects(camera, directions[ray], a, b, c, distance))
                        hits[ray] = std::min(hits[ray], distance);
                }
            }
        }
        Require(triangles > 0 && std::isfinite(diagonal) && diagonal > 0, "scene has no finite geometry");
        for (const auto distance : hits)
            Require(std::isfinite(distance) && distance > radius && distance <= diagonal * 1.01f,
                "initial camera lost floor, four-sided enclosure or forward geometry");
        Require(hits[0] <= 3.f, "initial camera is more than three meters above its floor");
        std::cout << scene.label << ": " << triangles << " transformed triangles, floor " << hits[0] << " m\n";
    }

    fs::path External(const fs::path& root, const char* uri)
    {
        Require(uri && *uri, "external resource URI is absent");
        std::string decoded = uri;
        decoded.resize(cgltf_decode_uri(decoded.data()));
        const auto path = root / Relative(decoded);
        Require(fs::is_regular_file(path) && !fs::is_symlink(path), "external glTF resource is missing");
        return path;
    }

    void CheckGltf(const fs::path& componentRoot, const Scene& scene, const Json& report)
    {
        cgltf_options options{};
        cgltf_data* raw = nullptr;
        const auto path = (componentRoot / scene.model).string();
        Require(cgltf_parse_file(&options, path.c_str(), &raw) == cgltf_result_success && raw,
            "retained glTF failed to parse");
        const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
        Require(data->file_type == cgltf_file_type_gltf && data->asset.version &&
            std::strcmp(data->asset.version, "2.0") == 0 && data->scene && data->scene->nodes_count > 0 &&
            data->meshes_count > 0 && data->accessors_count > 0 && data->buffers_count == 5 &&
            data->buffer_views_count == Integer(report, "bufferViewCount") &&
            data->images_count == scene.imageCount,
            "retained scene lost its glTF 2.0 structure or audited repack");
        std::set<const cgltf_buffer*> referenced;
        for (size_t i = 0; i < data->buffer_views_count; ++i)
        {
            Require(data->buffer_views[i].buffer != nullptr, "buffer view has no buffer");
            referenced.insert(data->buffer_views[i].buffer);
        }
        for (size_t i = 0; i < data->buffers_count; ++i)
            Require(referenced.count(&data->buffers[i]) == 1 &&
                fs::file_size(External(componentRoot, data->buffers[i].uri)) == data->buffers[i].size,
                "declared glTF buffer is unused or has the wrong byteLength");
        for (size_t i = 0; i < data->images_count; ++i)
        {
            const auto& image = data->images[i];
            Require(bool(image.uri) != bool(image.buffer_view), "image must have exactly one storage source");
            if (image.uri)
                (void)External(componentRoot, image.uri);
            else
                Require(image.mime_type && *image.mime_type, "embedded image has no MIME type");
        }
        Require(data->materials_count == scene.materialCount, "audited scene material count changed");
        for (size_t i = 0; i < data->materials_count; ++i)
            Require(data->materials[i].alpha_mode != cgltf_alpha_mode_blend && !data->materials[i].has_transmission,
                "scene retains a material domain the renderer cannot draw");
        Require(cgltf_load_buffers(&options, data.get(), path.c_str()) == cgltf_result_success &&
            cgltf_validate(data.get()) == cgltf_result_success, "CGltf rejected retained buffer/accessor data");
        CheckGeometry(*data, scene);
    }

    void CheckDescriptor(const fs::path& stagedRoot, const Scene& scene,
        const std::vector<uvsr::SceneCatalogEntry>& catalog)
    {
        const auto path = stagedRoot / scene.directory / (std::string(scene.directory) + ".scene.json");
        const auto descriptor = uvsr::json::Read(path);
        const auto model = "components/" + std::string(scene.model);
        const auto& models = Member(descriptor, "models", Kind::Array).array;
        const auto& graph = Member(descriptor, "graph", Kind::Array).array;
        Require(Member(descriptor, "displayName", Kind::String).string == scene.label && models.size() == 1 &&
            models[0].kind == Kind::String && models[0].string == model && graph.size() == 1 &&
            Member(graph[0], "model", Kind::Number).number == 0, "descriptor lost its single model instance");
        const auto* entry = uvsr::FindSceneCatalogEntry(catalog, path.generic_string());
        Require(entry && entry->DisplayName == scene.label && entry->InitialCamera &&
            !uvsr::FindSceneCatalogEntry(catalog, (path.parent_path() / model).generic_string()),
            "production catalog lost the descriptor or exposed its component");
        const auto& rawCamera = Member(descriptor, "initialCamera", Kind::Object);
        const auto& camera = *entry->InitialCamera;
        const std::array<std::pair<const char*, std::array<float, 3>>, 3> expected = {{
            { "position", scene.camera.Position }, { "direction", scene.camera.Direction }, { "up", scene.camera.Up }
        }};
        const std::array<std::array<float, 3>, 3> actual = { camera.Position, camera.Direction, camera.Up };
        for (size_t axis = 0; axis < expected.size(); ++axis)
        {
            const auto& values = Member(rawCamera, expected[axis].first, Kind::Array).array;
            Require(values.size() == 3, "camera vector has the wrong dimension");
            for (size_t i = 0; i < 3; ++i)
                Require(values[i].kind == Kind::Number && std::abs(values[i].number - expected[axis].second[i]) < 1e-5 &&
                    std::abs(actual[axis][i] - expected[axis].second[i]) < 1e-5f, "audited camera pose changed");
        }
        Require(std::abs(Member(rawCamera, "verticalFovDegrees", Kind::Number).number -
                scene.camera.VerticalFovDegrees) < 1e-4 &&
            std::abs(camera.VerticalFovDegrees - scene.camera.VerticalFovDegrees) < 1e-4f &&
            std::abs(XMVectorGetX(XMVector3Dot(Vector(camera.Direction), Vector(camera.Up)))) < 1e-5f,
            "production catalog changed the audited camera FOV or orthogonal axes");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "usage: uvsr_scene_asset_contract_tests <source-scenes> <staged-scenes> <runtime-asset-map>");
        const auto source = fs::absolute(argv[1]).lexically_normal();
        const auto staged = fs::absolute(argv[2]).lexically_normal();
        for (const auto& scene : Scenes)
            (void)Files(source / scene.directory);
        CheckLegal(source);
        CheckStaging(source, staged, argv[3]);
        std::vector<std::string> discovered;
        for (const auto& path : Files(staged, true))
        {
            const auto extension = Lower(fs::path(path).extension().string());
            if (extension == ".json" || extension == ".gltf" || extension == ".glb")
                discovered.push_back((staged / path).generic_string());
        }
        const auto catalog = uvsr::BuildSceneCatalog(staged, discovered);
        Require(catalog.size() == std::size(Scenes), "scene picker does not contain exactly the two retained scenes");
        for (const auto& scene : Scenes)
        {
            const auto report = CheckRepack(source / scene.directory, scene);
            CheckDescriptor(staged, scene, catalog);
            CheckGltf(staged / scene.directory / "components", scene, report);
        }
        CheckLightingAssets(source.parent_path() / "environments", source.parent_path() / "noise");
        std::cout << "scene, provenance, HDR and noise asset contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "asset contract failed: " << error.what() << '\n';
        return 1;
    }
}
