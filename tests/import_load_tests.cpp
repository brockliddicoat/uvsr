#include "renderer_import_load.h"
#include "renderer_scene_light.h"
#include "renderer_import_load_status.h"
#include "renderer_scene_load_worker.h"
#include "import_runtime_light_fixture.h"

#include <Windows.h>
#include <stb_image_write.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    using namespace uvsr;
    using Text = ArrayView<const char>;
    using Bytes = ArrayView<const uint8_t>;
    using Error = ImportError;
    using State = ImportLoadState;
    using Operation = ImportLoadOperation;
    size_t rejected = 0, retried = 0, canceled = 0;
    void Require(bool value, const char* reason)
    { if (!value) { fprintf(stderr, "load: %s\n", reason); exit(1); } }
    void Good(ImportResult result, const char* reason)
    {
        if (result) return;
        fprintf(stderr, "load: %s: %s, object %u, index %zu, parser %u, system %u\n", reason,
            ImportErrorText(result.error), unsigned(result.object), result.index, result.parserCode, result.systemCode);
        exit(1);
    }
    void Bad(ImportResult result, Error expected, const char* reason)
    {
        if (result.error != expected)
        {
            fprintf(stderr, "load: %s: expected %s, got %s, object %u, index %zu\n", reason,
                ImportErrorText(expected), ImportErrorText(result.error), unsigned(result.object), result.index); exit(1);
        }
        ++rejected;
    }
    Text Span(const char* value) { return {value, strlen(value)}; }
    Text Span(const std::string& value) { return {value.data(), value.size()}; }
    Bytes Data(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
    ImportSceneLoadOptions Options() { ImportSceneLoadOptions value; value.generation = 908; return value; }
    std::string Normalize(Text path)
    {
        std::string value(path.data, path.count);
        for (char& c : value) if (c == '\\') c = '/';
        return value;
    }
    struct MemoryFiles
    {
        struct File { std::string path; std::vector<uint8_t> bytes; };
        std::vector<File> files;
        Error existsFailure = Error::None;
        size_t longestPath = 0;
        void Put(const char* path, Bytes bytes)
        {
            auto* file = Find(Span(path));
            if (!file) { files.push_back({path, {}}); file = &files.back(); }
            file->bytes.clear();
            if (bytes.count) file->bytes.assign(bytes.data, bytes.data + bytes.count);
        }
        void Json(const char* path, const std::string& value) { Put(path, Data(value)); }
        File* Find(Text path)
        {
            const auto key = Normalize(path);
            for (auto& file : files) if (file.path == key) return &file;
            return nullptr;
        }
        static ImportResult Read(void* context, Text path, size_t limit, ImportFileData& output, ImportCancellation cancellation) noexcept
        {
            auto& self = *static_cast<MemoryFiles*>(context);
            if (path.count > self.longestPath) self.longestPath = path.count;
            if (cancellation.IsRequested()) return {Error::Canceled};
            auto* file = self.Find(path);
            if (!file) return {Error::FileUnavailable};
            if (file->bytes.size() > limit) return {Error::Capacity};
            ImportFileData candidate;
            auto result = candidate.Allocate(file->bytes.size());
            if (!result) return result;
            if (!file->bytes.empty()) memcpy(candidate.WritableBytes().data, file->bytes.data(), file->bytes.size());
            output = static_cast<ImportFileData&&>(candidate); return {};
        }
        static ImportResult Exists(void* context, Text path, bool& present) noexcept
        {
            auto& self = *static_cast<MemoryFiles*>(context);
            if (path.count > self.longestPath) self.longestPath = path.count;
            if (self.existsFailure != Error::None) return {self.existsFailure};
            present = self.Find(path) != nullptr; return {};
        }
        ImportFileSource Source() { return {Read, Exists, this}; }
    };
    const uint8_t pixels[]{255,0,0,255, 0,255,0,128, 0,0,255,0, 64,128,192,255};
    const float positions[]{0,0,0, 1,0,0, 0,1,0};
    const uint16_t indices[]{0,1,2};
    std::vector<uint8_t> Triangle()
    {
        std::vector<uint8_t> value(sizeof(positions) + sizeof(indices));
        memcpy(value.data(), positions, sizeof(positions));
        memcpy(value.data() + sizeof(positions), indices, sizeof(indices)); return value;
    }
    void PngWrite(void* context, void* data, int size)
    {
        Require(size >= 0, "PNG callback size");
        auto& value = *static_cast<std::vector<uint8_t>*>(context);
        const auto* bytes = static_cast<uint8_t*>(data);
        value.insert(value.end(), bytes, bytes + size);
    }
    std::vector<uint8_t> Png()
    {
        std::vector<uint8_t> value;
        Require(stbi_write_png_to_func(PngWrite, &value, 2, 2, 4, pixels, 8) != 0, "fixture PNG encoding"); return value;
    }
    std::string ModelJson(bool embedded = false, size_t imageBytes = 0)
    {
        std::string value = R"({"asset":{"version":"2.0"},"buffers":[{)";
        if (!embedded) value += R"("uri":"triangle.bin",)";
        value += "\"byteLength\":" + std::to_string(embedded ? 44 + imageBytes : 42);
        value += R"(}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6})";
        if (embedded) value += ",{\"buffer\":0,\"byteOffset\":44,\"byteLength\":" + std::to_string(imageBytes) + "}";
        value += R"(],"accessors":[{"bufferView":0,"componentType":5126,"type":"VEC3","count":3},{"bufferView":1,"componentType":5123,"type":"SCALAR","count":3}],"images":[{)";
        value += embedded ? R"("bufferView":2,"mimeType":"image/png")" : R"("uri":"pixels.png")";
        value += R"(}],"textures":[{"source":0}],"materials":[{"name":"paint","alphaMode":"BLEND","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],"meshes":[{"name":"triangle","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],"nodes":[{"name":"mesh","mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
        return value;
    }
    void Word(std::vector<uint8_t>& output, uint32_t value)
    { const size_t offset = output.size(); output.resize(offset + 4); memcpy(output.data() + offset, &value, 4); }
    std::vector<uint8_t> Glb()
    {
        const auto png = Png();
        std::string json = ModelJson(true, png.size());
        while (json.size() % 4) json.push_back(' ');
        auto bin = Triangle(); bin.resize(44); bin.insert(bin.end(), png.begin(), png.end());
        while (bin.size() % 4) bin.push_back(0);
        std::vector<uint8_t> value;
        Word(value, 0x46546c67); Word(value, 2); Word(value, uint32_t(28 + json.size() + bin.size()));
        Word(value, uint32_t(json.size())); Word(value, 0x4e4f534a);
        value.insert(value.end(), json.begin(), json.end());
        Word(value, uint32_t(bin.size())); Word(value, 0x004e4942);
        value.insert(value.end(), bin.begin(), bin.end()); return value;
    }
    MemoryFiles Files()
    {
        MemoryFiles value;
        const auto triangle = Triangle(), png = Png(), glb = Glb();
        value.Json("C:/fixtures/model.gltf", ModelJson());
        value.Put("C:/fixtures/triangle.bin", {triangle.data(), triangle.size()});
        value.Put("C:/fixtures/pixels.png", {png.data(), png.size()});
        value.Put("C:/fixtures/model.glb", {glb.data(), glb.size()});
        value.Json("C:/fixtures/scene.json", R"({"models":["model.gltf"],"graph":[{"name":"placed","model":0}]})");
        return value;
    }
    struct Trace
    {
        struct Event { ImportLoadProgress progress; std::string path; };
        struct Unavailable { uint32_t index; std::string path; Operation operation; ImportResult result; };
        std::vector<Event> events;
        std::vector<Unavailable> unavailable;
        size_t cancelAt = SIZE_MAX, running = 0;
        bool cancel = false;
        static bool Requested(void* context) noexcept { return static_cast<Trace*>(context)->cancel; }
        static void Report(void* context, const ImportLoadProgress& progress, Text path) noexcept
        {
            auto& self = *static_cast<Trace*>(context);
            self.events.push_back({progress, path.count ? std::string(path.data, path.count) : std::string{}});
            if (progress.state == State::Running && self.running++ == self.cancelAt) self.cancel = true;
        }
        static void ModelUnavailable(void* context, uint32_t index, Text path, Operation operation, ImportResult result) noexcept
        {
            auto& self = *static_cast<Trace*>(context);
            self.unavailable.push_back({index, std::string(path.data, path.count), operation, result});
        }
        ImportLoadCallbacks Callbacks() { return {{Requested, this}, Report, this, ModelUnavailable}; }
        void Check(State terminal) const
        {
            Require(!events.empty() && events.back().progress.state == terminal, "explicit terminal progress");
            ImportLoadProgress before;
            for (const auto& event : events)
            {
                const auto& p = event.progress;
                Require(p.objectsTotal >= before.objectsTotal && p.objectsCompleted >= before.objectsCompleted &&
                    p.objectsUnavailable >= before.objectsUnavailable && p.texturesTotal >= before.texturesTotal &&
                    p.texturesDecoded >= before.texturesDecoded && p.importStepsTotal >= before.importStepsTotal &&
                    p.importStepsCompleted >= before.importStepsCompleted && p.filesRead >= before.filesRead &&
                    p.fileBytesRead >= before.fileBytesRead, "progress counters regressed");
                Require(p.objectsUnavailable <= p.objectsCompleted && p.objectsCompleted <= p.objectsTotal &&
                    p.texturesDecoded <= p.texturesTotal && p.importStepsCompleted <= p.importStepsTotal, "progress bounds");
                Require(event.path.size() <= 511, "diagnostic path limit"); before = p;
            }
            if (terminal == State::Ready)
                Require(before.objectsCompleted == before.objectsTotal && before.texturesDecoded == before.texturesTotal &&
                    before.importStepsCompleted == before.importStepsTotal && before.result, "CPU-ready counters incomplete");
        }
    };
    struct Snapshot
    {
        std::vector<uint8_t> bytes;
        template<class T> void Value(const T& value)
        {
            const auto* data = reinterpret_cast<const uint8_t*>(&value);
            bytes.insert(bytes.end(), data, data + sizeof(T));
        }
        template<class T> void View(ArrayView<T> value)
        {
            Value(value.data); Value(value.count);
            if (value.count)
            {
                const auto* data = reinterpret_cast<const uint8_t*>(value.data);
                bytes.insert(bytes.end(), data, data + value.count * sizeof(T));
            }
        }
        explicit Snapshot(const ImportLoadedScene& value)
        {
            const auto s = value.scene.View();
            View(s.nodes); View(s.meshes); View(s.geometries); View(s.instances); View(s.materials); View(s.textures);
            View(s.bufferGroups); View(s.morphRanges); View(s.joints); View(s.lights); View(s.cameras); View(s.animations);
            View(s.channels); View(s.samplers); View(s.keyframes); View(s.strings); View(s.preorder);
            Value(s.root); Value(s.generation); Value(s.contentRevision); Value(s.materialRevision); Value(s.lightRevision);
            Value(s.transformRevision); Value(s.previousTransformRevision); Value(s.instanceTransformRevision);
            Value(s.previousInstanceTransformRevision); Value(value.scene.StorageBytes());
            Value(value.geometry.StorageBytes()); Value(value.geometry.ConversionScratchBytes()); Value(value.geometry.BufferCount());
            for (size_t i = 0; i < value.geometry.BufferCount(); ++i)
            {
                const auto b = value.geometry.Buffer(i);
                View(b.indices); View(b.vertices); View(b.morphs); View(b.jointMatrices); Value(b.indexOwner); Value(b.skinInstanceIndex);
            }
            const auto images = value.images.Images();
            Value(images.data); Value(images.count); Value(value.images.StorageBytes());
            for (size_t i = 0; i < images.count; ++i)
            {
                const auto& image = images.data[i];
                const auto info = image.Info();
                Value(info.format); Value(info.dimension); Value(info.alpha); Value(info.width); Value(info.height); Value(info.depth);
                Value(info.arraySize); Value(info.mipLevels); Value(info.originalBitsPerPixel); Value(info.allowGeneratedMips);
                View(image.Bytes()); View(image.Subresources()); Value(image.StorageBytes());
            }
            const auto& c = value.composition;
            Value(c.peakScratchBytes); Value(c.skippedParentSubtrees); Value(c.ignoredAnimationTargets);
            Value(c.ambiguousMaterialTargets); Value(c.skippedModelSubtrees);
            Value(value.runtimeLights.sun.generation); Value(value.runtimeLights.sun.index);
            Value(value.runtimeLights.flashlight.generation); Value(value.runtimeLights.flashlight.index);
            const auto& p = value.progress;
            Value(p.state); Value(p.operation); Value(p.objectsTotal); Value(p.objectsCompleted); Value(p.objectsUnavailable);
            Value(p.texturesTotal); Value(p.texturesDecoded); Value(p.modelIndex); Value(p.textureIndex);
            Value(p.importStepsTotal); Value(p.importStepsCompleted); Value(p.filesRead); Value(p.fileBytesRead); Value(p.pathTruncated);
            Value(p.result.error); Value(p.result.object); Value(p.result.index); Value(p.result.parserCode); Value(p.result.systemCode);
            Value(value.peakLoadingBytes);
        }
        void Unchanged(const ImportLoadedScene& value) const { Require(bytes == Snapshot(value).bytes, "failed transaction modified output"); }
    };
    void CheckTriangle(const ImportLoadedScene& value, uint32_t objectCount = 1)
    {
        const auto scene = value.scene.View();
        Require(scene.generation == 908 && scene.meshes.count == 1 && scene.geometries.count == 1 && scene.instances.count == 1 &&
            scene.materials.count == 1 && scene.textures.count == 1 && scene.bufferGroups.count == 1, "canonical triangle counts");
        Require(value.geometry.BufferCount() == 1, "owned geometry count");
        const auto buffer = value.geometry.Buffer(0);
        const uint32_t expectedIndices[]{0,1,2};
        Require(buffer.indices.count == sizeof(expectedIndices) && !memcmp(buffer.indices.data, expectedIndices, sizeof(expectedIndices)), "owned packed indices");
        const auto range = scene.bufferGroups.data[0].attributes[uint32_t(RendererSceneVertexAttribute::Position)];
        Require(range.offset <= buffer.vertices.count && sizeof(positions) <= buffer.vertices.count - size_t(range.offset) &&
            !memcmp(buffer.vertices.data + range.offset, positions, sizeof(positions)), "owned position stream");
        Require(value.images.Images().count == 1, "decoded array count");
        const auto& image = value.images.Images().data[0];
        const auto info = image.Info();
        Require(info.format == ImportImageFormat::SRGBA8_UNORM && info.width == 2 && info.height == 2 && info.originalBitsPerPixel == 32 &&
            info.alpha == RendererSceneTextureAlpha::Unknown, "retained PNG metadata");
        Require(image.Bytes().count == sizeof(pixels) && !memcmp(image.Bytes().data, pixels, sizeof(pixels)), "decoded literal RGBA pixels");
        Require(scene.textures.data[0].alpha == info.alpha && scene.textures.data[0].originalBitsPerPixel == 32, "canonical decoded metadata");
        Require(value.progress.state == State::Ready && value.progress.objectsTotal == objectCount &&
            value.progress.texturesDecoded == 1 && value.progress.texturesTotal == 1, "CPU-ready handoff");
    }
    ImportLoadedScene Loaded(MemoryFiles& files, const char* path = "C:/fixtures/model.gltf")
    { ImportLoadedScene value; Good(LoadImportScene(files.Source(), Span(path), Options(), value), "baseline load"); return value; }
    void OwnedAndProgress()
    {
        for (const char* path : {"C:/fixtures/model.gltf", "C:/fixtures/model.glb", "C:/fixtures/scene.json"})
        {
            auto files = Files(); Trace trace; ImportLoadedScene value;
            Good(LoadImportScene(files.Source(), Span(path), Options(), value, trace.Callbacks()), "direct/composed load");
            trace.Check(State::Ready); CheckTriangle(value);
            for (auto& file : files.files) memset(file.bytes.data(), 0xa5, file.bytes.size());
            files.files.clear(); CheckTriangle(value);
            ImportLoadedScene moved = static_cast<ImportLoadedScene&&>(value); CheckTriangle(moved);
            Require(!value.scene.StorageBytes() && !value.geometry.StorageBytes() && !value.images.StorageBytes(), "moved loading owners retained storage");
        }
    }
    void CapacitiesAndFailures()
    {
        auto files = Files(); auto output = Loaded(files); const Snapshot old(output);
        const char* path = "C:/fixtures/model.gltf";
        auto fail = [&](const ImportSceneLoadOptions& options, Error expected, const char* reason)
        {
            Trace trace;
            Bad(LoadImportScene(files.Source(), Span(path), options, output, trace.Callbacks()), expected, reason);
            trace.Check(State::Failed); old.Unchanged(output);
            ImportLoadedScene retry; Good(LoadImportScene(files.Source(), Span(path), Options(), retry), "failure retry"); CheckTriangle(retry); ++retried;
        };
        auto options = Options(); options.generation = 0; fail(options, Error::InvalidInput, "invalid generation");
        options = Options(); options.maxModels = 0; fail(options, Error::Capacity, "direct model limit");
        options = Options(); options.maxFileBytes = ModelJson().size() - 1; fail(options, Error::Capacity, "file limit");
        options = Options(); options.maxPathBytes = files.longestPath - 1; fail(options, Error::Capacity, "path limit");
        options = Options(); options.maxDecodedBytes = output.images.StorageBytes() - 1; fail(options, Error::Capacity, "decoded aggregate limit");
        {
            Trace trace; options = Options(); options.maxDecodedBytes = 0;
            Bad(LoadImportScene(files.Source(), Span(path), options, output, trace.Callbacks()), Error::Capacity, "decoded array limit");
            Require(trace.events.back().progress.operation == Operation::PrepareImages && trace.events.back().path.empty(), "decoded preparation failure phase");
            old.Unchanged(output);
        }
        options = Options(); options.maxGeometryBytes = output.geometry.StorageBytes() - 1; fail(options, Error::Capacity, "geometry limit");
        options = Options(); options.maxConversionScratchBytes = 0; fail(options, Error::Workspace, "conversion workspace limit");
        options = Options(); options.maxLoadingBytes = output.peakLoadingBytes - 1; fail(options, Error::Capacity, "loading path limit");
        options = Options(); options.maxEncodedImageBytes = 0; fail(options, Error::Capacity, "encoded ownership limit");
        options = Options(); options.maxFileBytes = ModelJson().size(); options.maxPathBytes = files.longestPath;
        options.maxLoadingBytes = output.peakLoadingBytes; options.maxDecodedBytes = output.images.StorageBytes();
        options.maxGeometryBytes = output.geometry.StorageBytes();
        ImportLoadedScene exact; Good(LoadImportScene(files.Source(), Span(path), options, exact), "exact owner capacities"); CheckTriangle(exact);
        for (const Error error : {Error::OutOfMemory, Error::Io})
        {
            files.existsFailure = error;
            Bad(LoadImportScene(files.Source(), Span(path), Options(), output), error, "checked image existence failure"); old.Unchanged(output);
            files.existsFailure = Error::None;
        }
        for (const char* badPath : {"C:/fixtures/missing.gltf", "C:/fixtures/empty.gltf", "C:/fixtures/broken.gltf"})
        {
            files.Json("C:/fixtures/empty.gltf", ""); files.Json("C:/fixtures/broken.gltf", "{");
            Trace trace; const auto result = LoadImportScene(files.Source(), Span(badPath), Options(), output, trace.Callbacks());
            Require(!result, "missing/malformed direct input accepted"); ++rejected; trace.Check(State::Failed); old.Unchanged(output);
        }
        files.Json("C:/fixtures/pixels.png", "malformed referenced image");
        Require(!LoadImportScene(files.Source(), Span(path), Options(), output), "malformed image accepted"); ++rejected; old.Unchanged(output);
        auto fresh = Files(); output = Loaded(fresh);
        for (const char* target : {"C:/fixtures/model.gltf", "C:/fixtures/model.glb", "C:/fixtures/scene.json"})
        {
            for (uint32_t kind = 0; kind < 2; ++kind)
            {
                bool finished = false;
                for (uint32_t ordinal = 0; ordinal < 1024; ++ordinal)
                {
                    const Snapshot before(output);
                    if (kind) SetRendererSceneAllocationFailure(ordinal + 1);
                    else SetImportAllocationFailureCountdown(ordinal);
                    const auto result = LoadImportScene(fresh.Source(), Span(target), Options(), output);
                    SetRendererSceneAllocationFailure(0); SetImportAllocationFailureCountdown(-1);
                    if (result) { CheckTriangle(output); finished = true; break; }
                    Bad(result, Error::OutOfMemory, "first-party allocation failure"); before.Unchanged(output);
                    ImportLoadedScene retry; Good(LoadImportScene(fresh.Source(), Span(target), Options(), retry), "allocation retry"); CheckTriangle(retry); ++retried;
                }
                Require(finished, "allocation fixture bound");
            }
        }
    }
    void PartialModels()
    {
        auto files = Files();
        files.Json("C:/fixtures/broken.gltf", "{");
        std::string shortModel = ModelJson();
        shortModel.replace(shortModel.find("triangle.bin"), strlen("triangle.bin"), "short.bin");
        files.Json("C:/fixtures/short.gltf", shortModel); files.Json("C:/fixtures/short.bin", "short");
        files.Json("C:/fixtures/partial.json", R"({"models":["missing.gltf","model.gltf","broken.gltf","short.gltf","unreferenced.gltf"],"graph":[{"name":"missing","model":0,"children":[{"name":"must skip","model":1}]},{"name":"valid","model":1},{"name":"broken","model":2,"children":[{"name":"must also skip"}]},{"name":"short","model":3},{"name":"missing twice","model":0}]})");
        Trace trace; ImportLoadedScene output;
        Good(LoadImportScene(files.Source(), Span("C:/fixtures/partial.json"), Options(), output, trace.Callbacks()), "partial description");
        trace.Check(State::Ready); CheckTriangle(output, 5);
        Require(output.progress.objectsUnavailable == 4 && output.composition.skippedModelSubtrees == 4 && output.scene.View().nodes.count == 3,
            "unavailable model ordinal/subtree semantics");
        Require(trace.unavailable.size() == 4 && trace.unavailable[0].index == 0 && trace.unavailable[1].index == 2 &&
            trace.unavailable[2].index == 3 && trace.unavailable[3].index == 4 &&
            trace.unavailable[0].path == "C:/fixtures/missing.gltf" && trace.unavailable[0].operation == Operation::ReadModel &&
            trace.unavailable[0].result.error == Error::FileUnavailable && trace.unavailable[1].operation == Operation::ParseModel &&
            trace.unavailable[2].operation == Operation::ReadBuffer, "exactly-once unavailable diagnostics");
        const auto scene = output.scene.View();
        const auto name = RendererSceneText(scene, scene.nodes.data[scene.nodes.data[scene.root].firstChildIndex].name);
        Require(name.count == 5 && !memcmp(name.data, "valid", 5), "available ordinal shifted after missing model");
        const Snapshot old(output);
        auto options = Options(); options.maxModels = 4;
        Bad(LoadImportScene(files.Source(), Span("C:/fixtures/partial.json"), options, output), Error::Capacity, "description model capacity"); old.Unchanged(output);
        options = Options(); options.maxLoadingBytes = output.peakLoadingBytes - 1;
        Bad(LoadImportScene(files.Source(), Span("C:/fixtures/partial.json"), options, output), Error::Capacity, "description loading capacity"); old.Unchanged(output);
        options.maxLoadingBytes = output.peakLoadingBytes;
        ImportLoadedScene exact; Good(LoadImportScene(files.Source(), Span("C:/fixtures/partial.json"), options, exact), "description exact capacity");
        std::string invalidModel = ModelJson();
        invalidModel.replace(invalidModel.find("\"POSITION\":0"), strlen("\"POSITION\":0"), "\"POSITION\":1");
        files.Json("C:/fixtures/model.gltf", invalidModel);
        Trace failure;
        Require(!LoadImportScene(files.Source(), Span("C:/fixtures/partial.json"), Options(), output, failure.Callbacks()), "conversion error became an unavailable model");
        ++rejected; failure.Check(State::Failed); old.Unchanged(output);
        Require(failure.events.back().progress.operation == Operation::ConvertModel, "conversion failure phase");
    }
    void Cancellation()
    {
        for (const char* path : {"C:/fixtures/model.gltf", "C:/fixtures/model.glb", "C:/fixtures/scene.json"})
        {
            auto files = Files(); auto output = Loaded(files); const Snapshot old(output);
            Trace baseline; ImportLoadedScene value;
            Good(LoadImportScene(files.Source(), Span(path), Options(), value, baseline.Callbacks()), "cancellation baseline");
            for (size_t event = 0; event < baseline.running; ++event)
            {
                Trace trace; trace.cancelAt = event;
                Bad(LoadImportScene(files.Source(), Span(path), Options(), output, trace.Callbacks()), Error::Canceled, "transition cancellation");
                trace.Check(State::Canceled); old.Unchanged(output); ++canceled;
                ImportLoadedScene retry; Good(LoadImportScene(files.Source(), Span(path), Options(), retry), "cancellation retry"); CheckTriangle(retry); ++retried;
            }
        }
        auto files = Files(); auto output = Loaded(files);
        const Snapshot old(output); Trace trace; trace.cancel = true;
        Bad(LoadImportScene(files.Source(), Span("C:/fixtures/scene.json"), Options(), output, trace.Callbacks()), Error::Canceled, "initial cancellation"); old.Unchanged(output);
        const std::string directory = "C:/" + std::string(600, 'a') + "/";
        auto longFiles = Files();
        for (auto& file : longFiles.files) file.path.replace(0, strlen("C:/fixtures/"), directory);
        Trace longTrace; ImportLoadedScene value;
        Good(LoadImportScene(longFiles.Source(), Span(directory + "model.glb"), Options(), value, longTrace.Callbacks()), "long full I/O path");
        CheckTriangle(value);
        Require(longTrace.events.front().path.size() == 511 && longTrace.events.front().progress.pathTruncated, "diagnostic-only path truncation");
        const auto longDescription = directory + "missing.json";
        longFiles.Json(longDescription.c_str(), R"({"models":["missing.gltf"],"graph":[{"model":0}]})");
        Trace unavailable;
        Good(LoadImportScene(longFiles.Source(), Span(longDescription), Options(), value, unavailable.Callbacks()), "long unavailable model event");
        Require(unavailable.unavailable.size() == 1 && unavailable.unavailable[0].path == directory + "missing.gltf",
            "full unavailable path truncated before synchronous logging");
    }
    struct PhysicalFiles
    {
        std::filesystem::path directory;
        std::vector<std::filesystem::path> files;
        explicit PhysicalFiles(const wchar_t* tag)
        {
            wchar_t module[32768]{};
            const DWORD length = GetModuleFileNameW(nullptr, module, DWORD(sizeof(module) / sizeof(module[0])));
            Require(length && length < sizeof(module) / sizeof(module[0]), "fixture executable directory");
            directory = std::filesystem::path(module).parent_path() /
                (L"load-" + std::wstring(tag) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
            Require(CreateDirectoryW(directory.c_str(), nullptr) != FALSE, "new physical fixture directory");
        }
        void Put(const wchar_t* name, Bytes bytes)
        {
            const auto path = directory / name;
            const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(file != INVALID_HANDLE_VALUE && bytes.count <= MAXDWORD, "new fixture file");
            DWORD written = 0;
            Require(WriteFile(file, bytes.data, DWORD(bytes.count), &written, nullptr) && written == bytes.count, "fixture file write");
            Require(CloseHandle(file) != FALSE, "fixture file close"); files.push_back(path);
        }
        std::string Path(const wchar_t* name) const { return (directory / name).u8string(); }
        ~PhysicalFiles()
        {
            for (const auto& file : files) Require(DeleteFileW(file.c_str()) != FALSE, "fixture file release");
            Require(RemoveDirectoryW(directory.c_str()) != FALSE, "empty fixture directory release");
        }
    };
    void NativeFiles()
    {
        const auto source = NativeImportFileSource();
        for (const wchar_t* location : {L"development", L"package"})
        {
            PhysicalFiles physical(location); const auto triangle = Triangle(), png = Png(), glb = Glb();
            const std::string model = ModelJson();
            physical.Put(L"m\u00f8d\u00e9l.gltf", Data(model));
            physical.Put(L"triangle.bin", {triangle.data(), triangle.size()});
            physical.Put(L"pixels.png", {png.data(), png.size()});
            physical.Put(L"model.glb", {glb.data(), glb.size()});
            const std::string description = R"({"models":["model.glb"],"graph":[{"name":"placed","model":0}]})";
            physical.Put(L"scene.json", Data(description)); physical.Put(L"empty.gltf", {});
            for (const wchar_t* name : {L"m\u00f8d\u00e9l.gltf", L"model.glb", L"scene.json"})
            {
                Trace trace; ImportLoadedScene value; const auto path = physical.Path(name);
                Good(LoadImportScene(source, Span(path), Options(), value, trace.Callbacks()), "physical development/package load");
                trace.Check(State::Ready); CheckTriangle(value);
            }
            ImportLoadedScene preserved;
            Good(LoadImportScene(source, Span(physical.Path(L"m\u00f8d\u00e9l.gltf")), Options(), preserved), "native allocation baseline");
            for (const wchar_t* name : {L"m\u00f8d\u00e9l.gltf", L"scene.json"})
            {
                const auto path = physical.Path(name);
                bool complete = false;
                for (uint32_t ordinal = 0; ordinal < 512; ++ordinal)
                {
                    const Snapshot original(preserved);
                    SetImportAllocationFailureCountdown(ordinal);
                    const auto result = LoadImportScene(source, Span(path), Options(), preserved);
                    SetImportAllocationFailureCountdown(-1);
                    if (result) { complete = true; CheckTriangle(preserved); break; }
                    Bad(result, Error::OutOfMemory, "native loading allocation failure"); original.Unchanged(preserved);
                    ImportLoadedScene retry;
                    Good(LoadImportScene(source, Span(path), Options(), retry), "native allocation retry"); CheckTriangle(retry); ++retried;
                }
                Require(complete, "native allocation fixture bound");
            }
            ImportFileData data; Good(data.Allocate(3), "file sentinel"); memset(data.WritableBytes().data, 0x6c, 3);
            const auto before = data.Bytes();
            Bad(source.read(nullptr, Span(physical.Path(L"model.glb")), glb.size() - 1, data, {}), Error::Capacity, "native size limit before allocation");
            Require(data.Bytes().data == before.data && data.Bytes().count == 3 && data.Bytes().data[0] == 0x6c, "native failure changed output");
            Good(source.read(nullptr, Span(physical.Path(L"model.glb")), glb.size(), data, {}), "native exact size limit");
            Require(data.Bytes().count == glb.size() && !memcmp(data.Bytes().data, glb.data(), glb.size()), "native exact bytes");
            const char invalidUtf8[]{'\xff', 'x'};
            const char embeddedNul[]{'a','\0','b'};
            for (Text invalid : {Text{invalidUtf8, 2}, Text{embeddedNul, 3}})
            {
                bool present = true;
                Bad(source.exists(nullptr, invalid, present), Error::InvalidInput, "invalid native path");
                Require(present, "failed existence changed output");
            }
            bool present = true;
            Good(source.exists(nullptr, Span(physical.Path(L"missing.gltf")), present), "missing existence"); Require(!present, "missing file exists");
            present = true;
            Good(source.exists(nullptr, Span(physical.directory.u8string()), present), "directory existence"); Require(!present, "directory is file");
            const auto missing = source.read(nullptr, Span(physical.Path(L"missing.gltf")), SIZE_MAX, data, {});
            Bad(missing, Error::FileUnavailable, "missing native file"); Require(missing.systemCode == ERROR_FILE_NOT_FOUND && !missing.parserCode, "system/parser error separation");
            Good(source.read(nullptr, Span(physical.Path(L"empty.gltf")), 0, data, {}), "empty native file"); Require(!data.Bytes().count, "empty file bytes");
            ImportLoadedScene output;
            Bad(LoadImportScene(source, Span(physical.Path(L"empty.gltf")), Options(), output), Error::InvalidData, "empty model content");
            std::vector<uint8_t> large(3 * 1024 * 1024 + 17, 0x53);
            physical.Put(L"large.bin", {large.data(), large.size()});
            struct CancelRead
            {
                size_t calls = 0;
                static bool Check(void* context) noexcept { return ++static_cast<CancelRead*>(context)->calls == 4; }
            } cancelRead;
            Good(data.Allocate(1), "chunk cancellation sentinel"); data.WritableBytes().data[0] = 0x71;
            const auto* pointer = data.Bytes().data;
            Bad(source.read(nullptr, Span(physical.Path(L"large.bin")), SIZE_MAX, data, {CancelRead::Check, &cancelRead}), Error::Canceled, "native cancellation after first chunk");
            Require(cancelRead.calls == 4 && data.Bytes().data == pointer && data.Bytes().data[0] == 0x71, "chunk cancellation changed output");
            Good(source.read(nullptr, Span(physical.Path(L"large.bin")), SIZE_MAX, data, {}), "chunk cancellation retry");
            Require(data.Bytes().count == large.size() && !memcmp(data.Bytes().data, large.data(), large.size()), "native complete chunked read");
        }
    }
    void Wait(HANDLE event) { Require(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "bounded fixture event"); }
    void Signal(HANDLE event) { Require(SetEvent(event) != FALSE, "fixture signal"); }
    struct WorkerLoad
    {
        MemoryFiles files = Files();
        ImportLoadedScene output;
        ImportLoadStatus status;
        HANDLE reached = CreateEventW(nullptr, TRUE, FALSE, nullptr), release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool held = false;
        State holdState = State::Running;
        Operation holdOperation = Operation::DecodeImage;
        ImportResult result;
        WorkerLoad() { Require(reached && release, "worker fixture events"); Good(status.Prepare(), "worker progress storage"); }
        ~WorkerLoad() { Require(CloseHandle(reached) && CloseHandle(release), "worker fixture event release"); }
        static bool Canceled(void* context) noexcept { return static_cast<const RendererSceneLoadCancellation*>(context)->IsRequested(); }
        static void Report(void* context, const ImportLoadProgress& progress, Text path) noexcept
        {
            auto& self = *static_cast<WorkerLoad*>(context);
            ImportLoadStatus::Report(&self.status, progress, path);
            if (!self.held && progress.operation == self.holdOperation && progress.state == self.holdState)
            { self.held = true; Signal(self.reached); Wait(self.release); }
        }
        static bool Work(void* context, const RendererSceneLoadCancellation& cancel)
        {
            auto& self = *static_cast<WorkerLoad*>(context);
            self.result = LoadImportScene(self.files.Source(), Span("C:/fixtures/scene.json"), Options(), self.output,
                {{Canceled, const_cast<RendererSceneLoadCancellation*>(&cancel)}, Report, &self});
            return bool(self.result);
        }
    };
    void WorkerAndStatus()
    {
        ImportLoadStatus status;
        SetImportAllocationFailureCountdown(0);
        Bad(status.Prepare(), Error::OutOfMemory, "status allocation"); SetImportAllocationFailureCountdown(-1);
        Require(status.Read().progress.state == State::Idle, "unprepared status");
        Good(status.Prepare(), "status retry"); Bad(status.Prepare(), Error::InvalidState, "status replacement while active");
        struct Writer
        {
            ImportLoadStatus& status;
            static bool Work(void* context, const RendererSceneLoadCancellation&)
            {
                auto& self = *static_cast<Writer*>(context);
                for (uint32_t i = 1; i <= 20000; ++i)
                {
                    ImportLoadProgress p; p.state = State::Running; p.objectsTotal = i; p.fileBytesRead = uint64_t(i) * 17;
                    char path[32]{}; const int length = snprintf(path, sizeof(path), "%u", i);
                    Require(length > 0, "status fixture number"); ImportLoadStatus::Report(&self.status, p, {path, size_t(length)});
                }
                return true;
            }
        } writer{status};
        RendererSceneLoadWorker writerThread;
        Require(writerThread.Start(Writer::Work, &writer), "status writer start");
        size_t reads = 0;
        do
        {
            const auto copy = status.Read();
            if (copy.progress.objectsTotal)
                Require(copy.progress.fileBytesRead == uint64_t(copy.progress.objectsTotal) * 17 &&
                    strtoul(copy.path, nullptr, 10) == copy.progress.objectsTotal, "torn progress snapshot");
            ++reads;
        } while (writerThread.GetState() == RendererSceneLoadWorkerState::Running);
        Require(writerThread.Join() && reads && status.Read().progress.objectsTotal == 20000, "status writer joined");
        status.Reset();
        for (bool cancel : {false, true})
        {
            WorkerLoad context; context.output = Loaded(context.files); const Snapshot before(context.output);
            RendererSceneLoadWorker worker;
            Require(worker.Start(WorkerLoad::Work, &context), "actual load worker start"); Wait(context.reached);
            const auto progress = context.status.Read();
            Require(progress.progress.state == State::Running && progress.progress.operation == Operation::DecodeImage &&
                progress.progress.objectsCompleted == 1 && progress.progress.texturesDecoded == 0, "worker copied in-progress state");
            if (cancel) worker.RequestCancel();
            Signal(context.release);
            const bool joined = worker.Join();
            if (cancel)
            {
                Require(!joined && worker.GetState() == RendererSceneLoadWorkerState::Cancelled, "canceled worker handoff accepted");
                Bad(context.result, Error::Canceled, "actual worker cancellation"); before.Unchanged(context.output);
                Require(context.status.Read().progress.state == State::Canceled, "canceled worker progress");
            }
            else
            {
                Require(joined, "load worker join"); Good(context.result, "actual worker result");
                for (auto& file : context.files.files) memset(file.bytes.data(), 0xcc, file.bytes.size());
                context.files.files.clear(); CheckTriangle(context.output);
                Require(context.status.Read().progress.state == State::Ready, "joined CPU-ready progress");
            }
        }
        {
            WorkerLoad context;
            auto active = Loaded(context.files); const Snapshot before(active);
            context.holdState = State::Ready; context.holdOperation = Operation::Finalize;
            RendererSceneLoadWorker worker;
            Require(worker.Start(WorkerLoad::Work, &context), "late cancellation worker start"); Wait(context.reached);
            Require(context.status.Read().progress.state == State::Ready && worker.GetState() == RendererSceneLoadWorkerState::Running,
                "CPU readiness incorrectly implies worker completion");
            worker.RequestCancel(); Signal(context.release);
            Require(!worker.Join() && worker.GetState() == RendererSceneLoadWorkerState::Cancelled && context.result,
                "late cancellation accepted a CPU-ready candidate");
            before.Unchanged(active); CheckTriangle(context.output);
            context.output = {};
        }
    }
    void CheckRuntimeLights(const ImportLoadedScene& output)
    {
        const auto scene = output.scene.View();
        const auto* sun = FindRendererSceneLight(scene, output.runtimeLights.sun);
        const auto* flashlight = FindRendererSceneLight(scene, output.runtimeLights.flashlight);
        Require(scene.lights.count == 2 && sun && flashlight && output.runtimeLights.sun != output.runtimeLights.flashlight &&
            sun->kind == RendererSceneLightKind::Directional && flashlight->kind == RendererSceneLightKind::Spot &&
            sun->values.irradiance == 8 && sun->values.angularSize == 0.2f && flashlight->values.intensity == 12 &&
            flashlight->values.range == 50 && flashlight->values.innerAngle == 7 && flashlight->values.outerAngle == 22,
            "complete runtime light values and generation-qualified IDs");
        const auto& sunNode = scene.nodes.data[sun->nodeIndex];
        const auto& flashlightNode = scene.nodes.data[flashlight->nodeIndex];
        Require(sunNode.parentIndex == scene.root && flashlightNode.parentIndex == scene.root &&
            sunNode.nextSiblingIndex == flashlight->nodeIndex && flashlightNode.nextSiblingIndex == InvalidSceneIndex,
            "runtime lights attach once at the final root tail");
        Require(sunNode.world.translation[0] == 2 && sunNode.world.translation[1] == 3 && sunNode.world.translation[2] == 4 &&
            sunNode.world.linear[0] == -1 && sunNode.world.linear[4] == 1 && sunNode.world.linear[8] == -1 &&
            flashlightNode.world.translation[0] == -2 && flashlightNode.world.translation[1] == 4 && flashlightNode.world.translation[2] == 8 &&
            !memcmp(&sunNode.world, &sunNode.previousWorld, sizeof(sunNode.world)) &&
            !memcmp(&flashlightNode.world, &flashlightNode.previousWorld, sizeof(flashlightNode.world)), "runtime initial world and previous poses");
    }
    void RuntimeLights()
    {
        auto files = Files();
        files.Json("C:/fixtures/two.json", R"({"models":["model.gltf","model.glb"],"graph":[{"name":"A","model":0},{"name":"B","model":1},{"name":"C","model":0}]})");
        const char* paths[]{"C:/fixtures/model.gltf", "C:/fixtures/model.glb", "C:/fixtures/scene.json", "C:/fixtures/two.json"};
        for (const char* path : paths)
        {
            ImportLoadedScene control, candidate;
            Good(LoadImportScene(files.Source(), Span(path), Options(), control), "runtime control without application lights");
            auto options = Options(); options.runtimeLights = tests::RuntimeLightFixtureOptions();
            Good(LoadImportScene(files.Source(), Span(path), options, candidate), "final runtime lights");
            CheckRuntimeLights(candidate);
            const auto before = control.scene.View(), after = candidate.scene.View();
            Require(after.nodes.count == before.nodes.count + 2 && after.instances.count == before.instances.count &&
                after.meshes.count == before.meshes.count && after.materials.count == before.materials.count &&
                after.textures.count == before.textures.count && candidate.geometry.BufferCount() == control.geometry.BufferCount(),
                "runtime lights preserve direct and composed resource selection");
            for (size_t n = 0; n < before.nodes.count; ++n)
            {
                const auto& a = before.nodes.data[n]; const auto& b = after.nodes.data[n];
                Require(before.preorder.data[n] == after.preorder.data[n] && a.parentIndex == b.parentIndex &&
                    a.leafKind == b.leafKind && a.leafIndex == b.leafIndex &&
                    !memcmp(&a.transform, &b.transform, sizeof(a.transform)) && !memcmp(&a.world, &b.world, sizeof(a.world)) &&
                    !memcmp(&a.previousWorld, &b.previousWorld, sizeof(a.previousWorld)), "runtime insertion preserves existing IDs and poses");
            }
            for (size_t g = 0; g < control.geometry.BufferCount(); ++g)
            {
                const auto a = control.geometry.Buffer(g), b = candidate.geometry.Buffer(g);
                Require(a.indices.count == b.indices.count && a.vertices.count == b.vertices.count &&
                    a.indexOwner == b.indexOwner && a.skinInstanceIndex == b.skinInstanceIndex &&
                    (!a.indices.count || !memcmp(a.indices.data, b.indices.data, a.indices.count)) &&
                    (!a.vertices.count || !memcmp(a.vertices.data, b.vertices.data, a.vertices.count)), "runtime insertion preserves packed geometry");
            }
            for (size_t t = 0; t < control.images.Images().count; ++t)
            {
                const auto a = control.images.Images().data[t].Bytes(), b = candidate.images.Images().data[t].Bytes();
                Require(a.count == b.count && (!a.count || !memcmp(a.data, b.data, a.count)), "runtime insertion preserves decoded images");
            }
            Good(LoadImportScene(files.Source(), Span(path), Options(), candidate), "runtime-light opt-out replaces aggregate");
            Require(!candidate.runtimeLights.sun && !candidate.runtimeLights.flashlight && !candidate.scene.View().lights.count,
                "disabled successful load clears old runtime IDs");
        }
        for (const char* path : {paths[0], paths[1], paths[2]})
        {
            auto options = Options(); options.runtimeLights = tests::RuntimeLightFixtureOptions();
            ImportLoadedScene output;
            Good(LoadImportScene(files.Source(), Span(path), options, output), "runtime transaction control");
            const Snapshot before(output);
            for (uint32_t invalid = 0; invalid < 9; ++invalid)
            {
                auto bad = options; Error expected = Error::InvalidInput;
                switch (invalid)
                {
                case 0: bad.runtimeLights.sun.name = {}; break;
                case 1: bad.runtimeLights.flashlight.name = {nullptr, 1}; break;
                case 2: bad.runtimeLights.flashlight.name = {"x\0y", 3}; break;
                case 3: bad.runtimeLights.sun.name = {"x", InvalidSceneIndex}; expected = Error::Capacity; break;
                case 4: bad.runtimeLights.flashlight.kind = RendererSceneLightKind::Point; break;
                case 5: bad.runtimeLights.sun.values.irradiance = NAN; expected = Error::InvalidData; break;
                case 6: bad.runtimeLights.flashlight.values.radius = NAN; expected = Error::InvalidData; break;
                case 7: bad.runtimeLights.flashlight.transform.translation[1] = INFINITY; expected = Error::InvalidData; break;
                case 8: bad.generation = 0; break;
                }
                Bad(LoadImportScene(files.Source(), Span(path), bad, output), expected, "invalid runtime spec"); before.Unchanged(output);
            }
            for (uint32_t kind = 0; kind < 2; ++kind)
            {
                bool completed = false;
                for (uint32_t ordinal = 0; ordinal < 1024; ++ordinal)
                {
                    const Snapshot old(output);
                    if (kind) SetRendererSceneAllocationFailure(ordinal + 1);
                    else SetImportAllocationFailureCountdown(ordinal);
                    const auto result = LoadImportScene(files.Source(), Span(path), options, output);
                    SetRendererSceneAllocationFailure(0); SetImportAllocationFailureCountdown(-1);
                    if (result) { CheckRuntimeLights(output); completed = true; break; }
                    Bad(result, Error::OutOfMemory, "runtime-light allocation failure"); old.Unchanged(output);
                    ImportLoadedScene retry;
                    Good(LoadImportScene(files.Source(), Span(path), options, retry), "runtime-light allocation retry"); CheckRuntimeLights(retry); ++retried;
                }
                Require(completed, "runtime-light allocation fixture bound");
            }
            Trace control;
            Good(LoadImportScene(files.Source(), Span(path), options, output, control.Callbacks()), "runtime cancellation control");
            for (size_t boundary = 0; boundary < control.running; ++boundary)
            {
                const Snapshot old(output);
                Trace trace; trace.cancelAt = boundary;
                Bad(LoadImportScene(files.Source(), Span(path), options, output, trace.Callbacks()), Error::Canceled, "runtime-light cancellation");
                old.Unchanged(output); trace.Check(State::Canceled); ++canceled;
            }
        }
        {
            std::string name = "owned runtime flashlight";
            auto options = Options(); options.runtimeLights = tests::RuntimeLightFixtureOptions(); options.runtimeLights.flashlight.name = Span(name);
            ImportLoadedScene output;
            Good(LoadImportScene(files.Source(), Span(paths[0]), options, output), "borrowed runtime name");
            memset(name.data(), 0xcc, name.size());
            const auto scene = output.scene.View();
            const auto* light = FindRendererSceneLight(scene, output.runtimeLights.flashlight);
            const auto owned = RendererSceneText(scene, scene.nodes.data[light->nodeIndex].name);
            Require(std::string(owned.data, owned.count) == "owned runtime flashlight", "runtime name survived caller mutation");
        }
        printf("runtime lights: final direct glTF/GLB and composed insertion, IDs, resources, images, rollback and cancellation passed\n");
    }
    void MetadataAndAvailability()
    {
        auto files = Files(); auto output = Loaded(files);
        const Snapshot old(output);
        Require(!output.scene.SetTextureMetadata({907, 0}, RendererSceneTextureAlpha::Opaque, 24).Succeeded(), "stale texture generation accepted");
        Require(!output.scene.SetTextureMetadata({908, 1}, RendererSceneTextureAlpha::Opaque, 24).Succeeded(), "texture index accepted");
        Require(!output.scene.SetTextureMetadata({908, 0}, RendererSceneTextureAlpha::Count, 24).Succeeded(), "texture alpha accepted"); old.Unchanged(output);
        const auto before = output.scene.View(); const auto original = before.textures.data[0];
        const auto same = output.scene.SetTextureMetadata({908, 0}, original.alpha, original.originalBitsPerPixel);
        Require(same.Succeeded() && !same.changed, "identical texture metadata changed revision"); old.Unchanged(output);
        const auto changed = output.scene.SetTextureMetadata({908, 0}, RendererSceneTextureAlpha::Opaque, 24);
        const auto after = output.scene.View(); const auto texture = after.textures.data[0];
        Require(changed.Succeeded() && changed.changed && after.contentRevision == before.contentRevision + 1 &&
            after.materialRevision == before.materialRevision && after.transformRevision == before.transformRevision &&
            texture.path.offset == original.path.offset && texture.path.length == original.path.length &&
            texture.mimeType.offset == original.mimeType.offset && texture.mimeType.length == original.mimeType.length &&
            texture.alpha == RendererSceneTextureAlpha::Opaque && texture.originalBitsPerPixel == 24, "texture metadata mutation scope");
        ImportSceneDescription description;
        const std::string json = R"({"models":["missing.gltf"],"graph":[{"name":"skip","model":0}]})";
        Good(description.Parse(Data(json), Span("C:/fixtures/main.json")), "availability description");
        ImportModel models[1]; RendererScene scene; ImportGeometry geometry; ImportTextures textures;
        ImportCompositionOptions options; options.generation = 909;
        ImportModelAvailability availability[]{ImportModelAvailability::Pending}; options.modelAvailability = availability;
        Bad(ComposeImportScene(description, models, options, scene, geometry, textures), Error::InvalidState, "pending model accepted");
        availability[0] = ImportModelAvailability::Unavailable;
        options.modelAvailability.count = 2;
        Bad(ComposeImportScene(description, models, options, scene, geometry, textures), Error::InvalidInput, "availability count mismatch");
        options.modelAvailability.count = 1;
        models[0].scene = static_cast<RendererScene&&>(output.scene);
        Bad(ComposeImportScene(description, models, options, scene, geometry, textures), Error::InvalidState, "unavailable payload accepted");
        models[0].scene.Reset();
        ImportCompositionStats stats;
        Good(ComposeImportScene(description, models, options, scene, geometry, textures, &stats), "unavailable empty slot");
        Require(scene.View().nodes.count == 1 && stats.skippedModelSubtrees == 1, "unavailable node synthesized");
    }
}

int main()
{
    OwnedAndProgress(); CapacitiesAndFailures(); PartialModels(); Cancellation(); NativeFiles(); WorkerAndStatus(); MetadataAndAvailability();
    RuntimeLights();
    printf("import load: direct/composed/physical bytes, metadata, partial subtrees, %zu rejected transactions, %zu retries, %zu transition cancellations, native chunk cancellation, joined workers and coherent progress passed\n",
        rejected, retried, canceled);
    return 0;
}
