#include "renderer_import_path.h"
#include "renderer_import_description.h"

// the private std::filesystem oracle exercises the retained platform grammar.
// runtime path operations expose only byte views and use no standard containers.
#include <filesystem>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace
{
    using namespace uvsr;
    namespace fs = std::filesystem;
    size_t comparisons = 0, rejections = 0, fileImports = 0;

    void Require(bool condition, const char* message)
    {
        if (!condition) { fprintf(stderr, "import path: %s\n", message); exit(1); }
    }
    ArrayView<const char> Text(const char* value) { return {value, strlen(value)}; }
    ArrayView<const char> Text(const std::string& value) { return {value.data(), value.size()}; }

    void Compare(const std::string& file, const std::string& reference, const std::string& expected)
    {
        size_t length = SIZE_MAX;
        Require(bool(MeasureImportPath(Text(file), Text(reference), length)), "measure failed");
        std::vector<char> output(length + 3, char(0x5a));
        size_t written = SIZE_MAX;
        Require(bool(ResolveImportPath(Text(file), Text(reference), {output.data(), length + 1}, written)), "resolve failed");
        if (written != length || std::string(output.data(), written) != expected)
        {
            fprintf(stderr, "file=[%s] reference=[%s]\nexpected=[%s] actual=[%s]\n", file.c_str(), reference.c_str(), expected.c_str(), output.data());
            Require(false, "native path mismatch");
        }
        Require(output[length] == 0 && output[length + 1] == char(0x5a) && output[length + 2] == char(0x5a), "terminator or output guard");
        output.assign(length + 3, char(0x5a));
        written = 987;
        Require(ResolveImportPath(Text(file), Text(reference), {output.data(), length}, written).error == ImportError::Capacity, "short capacity accepted");
        Require(written == 987, "failure changed length");
        for (char value : output) Require(value == char(0x5a), "failure changed output");
        ++comparisons;
        ++rejections;
    }

    void PathOracle()
    {
        const char* files[]{"", "model.gltf", ".", "..", "./model.gltf", "assets/model.glb", "assets//model.gltf",
            "assets/./part/../model.gltf", "/model.gltf", "//model.gltf", "///model.gltf", "/assets//model.gltf",
            "C:", "C:model.gltf", "C:assets/model.gltf", "C:/model.gltf", "C://model.gltf", "C:/assets/model.gltf",
            "c:\\assets\\model.gltf", "C:/assets//model.gltf", "C:/assets/", "//server", "//server/",
            "//server/share/model.gltf", "\\\\server\\share\\dir\\model.glb", "//server//share//model.gltf",
            "\\\\?\\C:\\assets\\model.gltf", "\\\\?\\UNC\\server\\share\\model.gltf", "\\??\\C:\\assets\\model.gltf",
            "\\\\.\\device\\model.gltf", "//?/", "//?//model.gltf", "C:/a b/50%/model.gltf"};
        const char* references[]{"", "image.png", "nested/image.png", "nested//image.png", "../image.png", "./image.png",
            ".", "..", "/image.png", "//image.png", "///image.png", "\\image.png", "C:", "c:", "D:", "C:images/image.png",
            "c:images/image.png", "D:images/image.png", "C:/images/image.png", "C://images/image.png", "//server/share/image.png",
            "//other/share/image.png", "//server", "\\\\server\\share\\image.png", "\\\\?\\C:\\image.png", "\\??\\C:\\image.png",
            "\\\\.\\device\\image.png", "dir/image%20.png", "dir/image name.png", "dir/50%/image.png", "nested/", "nested\\"};
#if defined(_WIN32)
        for (const char* file : files)
            for (const char* reference : references)
                Compare(file, reference, (fs::path(file).parent_path() / fs::path(reference)).generic_string());
        const std::string longFile = "C:/" + std::string(30000, 'a') + "/model.gltf";
        const std::string longReference = std::string(31000, 'b') + "/image.png";
        Compare(longFile, longReference, (fs::path(longFile).parent_path() / fs::path(longReference)).generic_string());
#else
        (void)files; (void)references;
#endif
        Compare("C:/assets/model.gltf", "/image.png", "C:/image.png");
        Compare("//server/share/model.gltf", "/image.png", "//server/image.png");
        Compare("C:/assets/model.gltf", "C:image.png", "C:/assets/image.png");
        Compare("C:/assets/model.gltf", "c:image.png", "c:image.png");
        Compare("C:/assets//model.gltf", "nested//image.png", "C:/assets/nested//image.png");
        Compare("model.gltf", "image%20.png", "image%20.png");
    }

    void InvalidInputs()
    {
        char output[64];
        const char embeddedNull[]{'a', '\0', 'b'};
        const ArrayView<const char> bad[]{{nullptr, 1}, {embeddedNull, sizeof(embeddedNull)}, {"x", SIZE_MAX},
            {reinterpret_cast<const char*>(UINTPTR_MAX - 2), 8}};
        for (const auto input : bad)
            for (bool badFile : {false, true})
            {
                memset(output, 0x5a, sizeof(output));
                size_t length = 987;
                const auto file = badFile ? input : Text("dir/model.gltf");
                const auto reference = badFile ? Text("image.png") : input;
                Require(MeasureImportPath(file, reference, length).error == ImportError::InvalidInput && length == 987, "invalid measure");
                Require(ResolveImportPath(file, reference, output, length).error == ImportError::InvalidInput && length == 987, "invalid resolve");
                for (char value : output) Require(value == char(0x5a), "invalid input changed output");
                ++rejections;
            }
        const ArrayView<char> badOutput[]{{nullptr, 100}, {output, SIZE_MAX}, {reinterpret_cast<char*>(UINTPTR_MAX - 2), 8}};
        for (const auto view : badOutput)
        {
            size_t length = 987;
            Require(ResolveImportPath(Text("model.gltf"), Text("image.png"), view, length).error == ImportError::InvalidOutput && length == 987, "invalid output accepted");
            ++rejections;
        }
        memcpy(output, "dir/model.gltf", sizeof("dir/model.gltf"));
        const auto original = std::string(output);
        for (bool overlapFile : {false, true})
        {
            size_t length = 987;
            const auto file = overlapFile ? Text(output) : Text("dir/model.gltf");
            const auto reference = overlapFile ? Text("image.png") : Text(output + 4);
            Require(ResolveImportPath(file, reference, output, length).error == ImportError::InvalidOutput && length == 987, "overlap accepted");
            Require(std::string(output) == original, "overlap changed input");
            ++rejections;
        }
        size_t length = 987;
        SetImportAllocationFailureCountdown(0);
        Require(bool(ResolveImportPath(Text("dir/model.gltf"), Text("image.png"), output, length)), "path operation allocated import storage");
        SetImportAllocationFailureCountdown(-1);
        Require(std::string(output) == "dir/image.png", "path operation result");
    }

    struct Files
    {
        fs::path created[48];
        size_t count = 0;
        ~Files()
        {
            std::error_code error;
            while (count) { fs::remove(created[--count], error); Require(!error, "fixture cleanup failed"); }
        }
        void Remember(const fs::path& path)
        {
            Require(count < 48, "fixture path capacity");
            created[count++] = path;
        }
        void Directory(const fs::path& path)
        {
            std::error_code error;
            Require(fs::create_directory(path, error) && !error, "fixture directory creation");
            Remember(path);
        }
        FILE* Open(const fs::path& path, bool write)
        {
            FILE* file = nullptr;
#if defined(_WIN32)
            Require(_wfopen_s(&file, path.c_str(), write ? L"wb" : L"rb") == 0 && file, "fixture open");
#else
            file = fopen(path.c_str(), write ? "wb" : "rb");
            Require(file != nullptr, "fixture open");
#endif
            return file;
        }
        void Write(const fs::path& path, ArrayView<const uint8_t> bytes)
        {
            FILE* file = Open(path, true);
            Require(fwrite(bytes.data, 1, bytes.count, file) == bytes.count && fclose(file) == 0, "fixture write");
            Remember(path);
        }
        std::vector<uint8_t> Read(const std::string& path)
        {
            FILE* file = Open(fs::u8path(path), false);
            Require(fseek(file, 0, SEEK_END) == 0, "fixture seek");
            const long size = ftell(file);
            Require(size >= 0 && fseek(file, 0, SEEK_SET) == 0, "fixture size");
            std::vector<uint8_t> result(static_cast<size_t>(size));
            Require(fread(result.data(), 1, result.size(), file) == result.size() && fclose(file) == 0, "fixture read");
            return result;
        }
    };

    std::string Resolve(const std::string& file, const std::string& reference)
    {
        size_t length = 0;
        Require(bool(MeasureImportPath(Text(file), Text(reference), length)), "file path measure");
        std::vector<char> output(length + 1);
        Require(bool(ResolveImportPath(Text(file), Text(reference), {output.data(), output.size()}, length)), "file path resolve");
        return {output.data(), length};
    }

    void PhysicalLocations()
    {
        Files files;
        std::error_code error;
        const auto temporary = fs::temp_directory_path(error);
        Require(!error, "temporary directory");
#if defined(_WIN32)
        const int process = _getpid();
#else
        const int process = int(getpid());
#endif
        fs::path root;
        for (unsigned attempt = 0; attempt < 64; ++attempt)
        {
            root = temporary / ("uvsr-import-path-" + std::to_string(process) + "-" + std::to_string(attempt));
            if (fs::create_directory(root, error)) { files.Remember(root); break; }
            Require(!error || error == std::errc::file_exists, "temporary creation error");
            Require(attempt != 63, "temporary namespace exhausted");
            error.clear();
        }
        const float expected[]{1.f, -2.f, 3.5f};
        const std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"vertex%20%2520.bin","byteLength":12}],"bufferViews":[{"buffer":0,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":1,"type":"VEC3"}]})";
        const std::string embedded = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":12}],"bufferViews":[{"buffer":0,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":1,"type":"VEC3"}]})";
        for (const char* location : {"development", "package"})
        {
            auto directory = root / location;
            files.Directory(directory);
            directory /= "assets"; files.Directory(directory);
            directory /= "scenes"; files.Directory(directory);
            directory /= "room"; files.Directory(directory);
            const std::string descriptor = (directory / "room.scene.json").generic_u8string();
            const std::string descriptionJson = R"({"models":["components/model.gltf","components/../components/model.glb"]})";
            files.Write(fs::u8path(descriptor), {reinterpret_cast<const uint8_t*>(descriptionJson.data()), descriptionJson.size()});
            directory /= "components"; files.Directory(directory);
            files.Write(directory / "vertex %20.bin", {reinterpret_cast<const uint8_t*>(expected), sizeof(expected)});
            files.Write(directory / "model.gltf", {reinterpret_cast<const uint8_t*>(json.data()), json.size()});
            std::string padded = embedded;
            while (padded.size() % 4) padded.push_back(' ');
            std::vector<uint8_t> glb(20 + padded.size() + 8 + sizeof(expected));
            const uint32_t header[]{0x46546c67, 2, uint32_t(glb.size()), uint32_t(padded.size()), 0x4e4f534a};
            const uint32_t binary[]{uint32_t(sizeof(expected)), 0x004e4942};
            memcpy(glb.data(), header, sizeof(header));
            memcpy(glb.data() + 20, padded.data(), padded.size());
            memcpy(glb.data() + 20 + padded.size(), binary, sizeof(binary));
            memcpy(glb.data() + 28 + padded.size(), expected, sizeof(expected));
            files.Write(directory / "model.glb", {glb.data(), glb.size()});
            auto descriptorInput = files.Read(descriptor);
            ImportSceneDescription description;
            Require(bool(description.Parse({descriptorInput.data(), descriptorInput.size()}, Text(descriptor))), "physical scene description parse");
            memset(descriptorInput.data(), 0xa5, descriptorInput.size());
            descriptorInput.clear(); descriptorInput.shrink_to_fit();
            Require(description.ModelCount() == 2, "physical description model count");
            for (size_t model = 0; model < description.ModelCount(); ++model)
            {
                const auto path = description.ModelPath(model);
                const std::string modelPath(path.data, path.count);
                auto input = files.Read(modelPath);
                ImportDocument document;
                Require(bool(document.Parse({input.data(), input.size()})), "physical model parse");
                memset(input.data(), 0xa5, input.size());
                ImportBufferInfo buffer;
                Require(bool(document.BufferInfo(0, buffer)), "physical buffer info");
                if (!buffer.resident)
                {
                    Require(std::string(buffer.uri.data, buffer.uri.count) == "vertex %20.bin", "URI did not decode exactly once");
                    auto bytes = files.Read(Resolve(modelPath, std::string(buffer.uri.data, buffer.uri.count)));
                    Require(bool(document.SupplyBuffer(0, {bytes.data(), bytes.size()})), "physical buffer supply");
                    memset(bytes.data(), 0xa5, bytes.size());
                }
                float actual[3]{};
                Require(bool(document.ReadFloats(0, actual)) && memcmp(actual, expected, sizeof(expected)) == 0, "physical accessor bytes");
                document.Reset();
                ++fileImports;
            }
        }
    }
}

int main()
{
    PathOracle();
    InvalidInputs();
    PhysicalLocations();
    printf("import path: %zu exact comparisons, %zu preserved-output rejections, %zu physical development/package imports\n", comparisons, rejections, fileImports);
    return 0;
}
