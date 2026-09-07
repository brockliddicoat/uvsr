#include "renderer_shader_factory.h"
#include "shader_blob.h"

#include <process.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace uvsr
{
    struct RendererShaderFactoryTestAccess
    {
        static auto Path(const fs::path& root, const char* file, const char* entry)
        {
            return RendererShaderFactory::ResolveBlobPath(root, file, entry);
        }
        static std::optional<std::string> Select(RendererShaderFactory& factory,
            const char* file, const char* entry, const std::vector<RendererShaderMacro>* macros = nullptr)
        {
            const auto value = factory.SelectBytecode(file, entry, macros);
            return value ? std::optional<std::string>(
                std::string(static_cast<const char*>(value->data), value->size)) : std::nullopt;
        }
    };
}

namespace
{
    using namespace uvsr;
    using Access = RendererShaderFactoryTestAccess;
    struct Row { std::string key; fs::path object; };
    using Catalog = std::map<std::string, std::vector<Row>>;

    void Require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    std::string Read(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(bool(stream), "cannot read " + path.string());
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }
    void Write(const fs::path& path, const std::string& value)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(value.data(), static_cast<std::streamsize>(value.size()));
        Require(bool(stream), "cannot write fixture " + path.string());
    }
    bool Run(const fs::path& executable, std::vector<std::string> arguments)
    {
        arguments.insert(arguments.begin(), executable.string());
        std::vector<const char*> raw;
        for (const auto& argument : arguments) raw.push_back(argument.c_str());
        raw.push_back(nullptr);
        return _spawnv(_P_WAIT, executable.string().c_str(), raw.data()) == 0;
    }
    std::optional<std::string> Select(const std::string& bytes, const std::string& key)
    {
        std::vector<std::string> names, values;
        std::istringstream input(key);
        for (std::string token; input >> token;)
        {
            const auto equal = token.find('=');
            Require(equal != std::string::npos, "catalog macro omits its value");
            names.push_back(token.substr(0, equal)); values.push_back(token.substr(equal + 1));
        }
        std::vector<shader_blob::Constant> constants;
        for (size_t index = names.size(); index > 0; --index)
            constants.push_back({ names[index - 1].c_str(), values[index - 1].c_str() });
        const void* data = nullptr;
        size_t size = 0;
        if (!shader_blob::find_permutation(bytes.data(), bytes.size(), constants.data(),
                static_cast<uint32_t>(constants.size()), &data, &size))
            return std::nullopt;
        return std::string(static_cast<const char*>(data), size);
    }

    Catalog ReadCatalog(const fs::path& root)
    {
        Catalog catalog;
        for (const auto& file : fs::directory_iterator(root / "catalogs"))
        {
            if (file.path().extension() != ".txt") continue;
            std::istringstream input(Read(file.path()));
            for (std::string line; std::getline(input, line);)
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                const auto tab = line.find('\t');
                Require(tab != std::string::npos && line.find('\t', tab + 1) == std::string::npos,
                    "malformed compiled shader catalog");
                const fs::path object = line.substr(tab + 1);
                auto relative = fs::relative(object, root / "objects");
                const auto stem = relative.stem().string();
                const auto hash = stem.rfind('.');
                Require(hash != std::string::npos, "compiled shader lacks task identity");
                relative.replace_filename(stem.substr(0, hash));
                catalog[relative.generic_string()].push_back({ line.substr(0, tab), object });
            }
        }
        Require(!catalog.empty(), "compiled shader catalog is empty");
        return catalog;
    }

    void CheckBundle(const fs::path& inventory, const fs::path& framework,
        const fs::path& application, const fs::path& runtime)
    {
        const auto appCatalog = ReadCatalog(application), frameworkCatalog = ReadCatalog(framework);
        const std::map<std::string, size_t> retained = {
            { "agx_tonemapping_ps", 4 },
            { "auto_exposure_histogram_cs", 1 },
            { "auto_exposure_resolve_cs", 1 },
            { "directional_ray_visibility_cs", 1 },
            { "display_output_ps", 1 },
            { "fast_approximate_aa_ps", 1 },
            { "image_based_lighting_background_ps", 1 },
            { "light_probe_processing_cubemap_gs", 1 },
            { "light_probe_processing_environment_brdf_ps", 1 },
            { "light_probe_processing_mip_ps", 1 },
            { "light_probe_processing_specular_probe_ps", 1 },
            { "lighting_accumulation_cs", 1 },
            { "lighting_accumulation_prepare_cs", 1 },
            { "material_id_ps", 2 },
            { "path_tracing_cs", 1 },
            { "pbr_deferred_lighting_cs", 1 },
            { "pbr_gbuffer_ps", 4 },
            { "pixel_zoom_ps", 1 },
            { "ray_traced_flashlight_shadows_cs_GenerateVisibility", 1 },
            { "ray_traced_sky_visibility_cs_Generate", 1 },
            { "renderer_blit_ps", 1 },
            { "renderer_fullscreen_vs", 2 },
            { "renderer_gbuffer_vs_buffer_loads", 1 },
            { "renderer_pixel_readback_cs", 1 },
        };
        Require(appCatalog.size() == retained.size(), "retained shader family set changed");
        for (const auto& [family, count] : retained)
            Require(appCatalog.count(family) && appCatalog.at(family).size() == count,
                "retained permutations changed for " + family);

        const auto requireKeys = [&](const std::string& family, const std::set<std::string>& expectedKeys) {
            std::set<std::string> actualKeys;
            for (const auto& row : appCatalog.at(family)) actualKeys.insert(row.key);
            Require(actualKeys == expectedKeys, "retained option domain changed for " + family);
        };
        const auto axis = [&](const std::string& family, const std::string& name,
                std::initializer_list<unsigned> values) {
            std::set<std::string> keys;
            for (unsigned value : values) keys.insert(name + '=' + std::to_string(value));
            requireKeys(family, keys);
        };
        axis("material_id_ps", "ALPHA_TESTED", { 0, 1 });
        requireKeys("renderer_gbuffer_vs_buffer_loads", { "" });
        std::set<std::string> expected, actual, packagedAppFamilies;
        std::istringstream lines(Read(inventory));
        std::string previous;
        for (std::string line; std::getline(lines, line);)
        {
            Require(line.rfind("bin/shaders/", 0) == 0 && line > previous &&
                line.find_first_of("\\:\r") == std::string::npos, "noncanonical shader inventory");
            previous = line;
            const std::string relative = line.substr(12);
            const fs::path path(relative);
            Require(path.extension() == ".bin" && path.lexically_normal().generic_string() == relative,
                "unsafe shader inventory path");
            for (const auto& part : path)
                Require(part != ".." && part != ".", "shader inventory escapes its root");
            const bool app = relative.rfind("uvsr/dxil/", 0) == 0;
            Require(app || relative.rfind("framework/dxil/", 0) == 0, "unknown shader owner");
            const std::string family = relative.substr(app ? 10 : 15, relative.size() - (app ? 10 : 15) - 4);
            const auto& catalog = app ? appCatalog : frameworkCatalog;
            Require(catalog.count(family) != 0, "staged family has no compiled catalog: " + family);
            const auto bytes = Read((app ? application : framework) / "dxil" / (family + ".bin"));
            Require(!bytes.empty() && bytes == Read(runtime / path),
                "runtime shader differs from compiled family: " + relative);
            std::set<std::string> keys;
            for (const auto& row : catalog.at(family))
            {
                Require(keys.insert(row.key).second, "duplicate compiled permutation");
                Require(Select(bytes, row.key) == Read(row.object),
                    "packed selection differs from compiled DXIL: " + family + " " + row.key);
            }
            std::vector<std::string> packedKeys;
            shader_blob::enumerate_permutations(bytes.data(), bytes.size(), packedKeys);
            if (!(keys.size() == 1 && keys.count("") == 1))
                Require(std::set<std::string>(packedKeys.begin(), packedKeys.end()) == keys &&
                    packedKeys.size() == keys.size(), "blob contains missing or extra permutations");
            expected.insert(relative);
            if (app) packagedAppFamilies.insert(family);
        }
        Require(expected.size() == 33 && packagedAppFamilies.size() == retained.size(),
            "package omitted a retained shader family");
        for (const auto& entry : fs::recursive_directory_iterator(runtime))
        {
            Require(!entry.is_symlink(), "shader package contains a link");
            if (entry.is_directory()) continue;
            Require(entry.is_regular_file(), "shader package contains a non-file");
            actual.insert(fs::relative(entry.path(), runtime).generic_string());
        }
        Require(actual == expected, "runtime shader tree differs from exact inventory");
    }

    void CheckFactory(const fs::path& root)
    {
        Require(Access::Path(root, "uvsr/path_tracing_cs.hlsl", "main") == root / "path_tracing_cs.bin" &&
            Access::Path(root, "uvsr/sky.hlsl", "Generate") == root / "sky_Generate.bin",
            "factory entry naming changed");
        for (const char* file : { "", "framework/a.hlsl", "uvsr/../a.hlsl", "uvsr/./a.hlsl",
                "uvsr\\a.hlsl", "C:/a.hlsl", "uvsr/a.bin", "uvsr/passes/a.hlsl" })
            Require(!Access::Path(root, file, "main"), "factory accepted unsafe shader path");
        for (const char* entry : { "", "../entry" })
            Require(!Access::Path(root, "uvsr/a.hlsl", entry), "factory accepted unsafe entry");
        RendererShaderFactory factory(nullptr, root);
        const std::string first = "first", second = "second";
        Write(root / "cache.bin", first);
        Require(Access::Select(factory, "uvsr/cache.hlsl", "main") == first, "initial factory load failed");
        Write(root / "cache.bin", second);
        Require(Access::Select(factory, "uvsr/cache.hlsl", "main") == first, "cache lost loaded bytes");
        factory.ClearCache();
        Require(Access::Select(factory, "uvsr/cache.hlsl", "main") == second, "cache clear did not reload");
        Write(root / "retry.bin", "");
        Require(!Access::Select(factory, "uvsr/retry.hlsl", "main"), "factory accepted empty bytes");
        Write(root / "retry.bin", second);
        Require(Access::Select(factory, "uvsr/retry.hlsl", "main") == second, "failed load prevented retry");
        Write(root / "sky_Generate.bin", second);
        Require(Access::Select(factory, "uvsr/sky.hlsl", "Generate") == second, "named entry load failed");
        std::ostringstream packed(std::ios::binary | std::ios::out);
        Require(shader_blob::write_header(packed) &&
            shader_blob::write_permutation(packed, "ALPHA=1 BETA=2", first.data(), first.size()) &&
            shader_blob::write_permutation(packed, "ALPHA=2 BETA=1", second.data(), second.size()),
            "packed fixture creation failed");
        Write(root / "packed.bin", packed.str());
        std::vector<RendererShaderMacro> macros = { { "BETA", "2" }, { "ALPHA", "1" } };
        Require(Access::Select(factory, "uvsr/packed.hlsl", "main", &macros) == first &&
            !Access::Select(factory, "uvsr/cache.hlsl", "main", &macros), "factory macro selection changed");
        macros[0].definition = "9";
        Require(!Access::Select(factory, "uvsr/packed.hlsl", "main", &macros),
            "factory accepted a missing permutation");
        Require(!factory.CreateShader("uvsr/cache.hlsl", "main", nullptr, nvrhi::ShaderType::Compute),
            "factory without a device created a GPU shader");
        std::ostringstream one(std::ios::binary | std::ios::out);
        Require(shader_blob::write_header(one) &&
            shader_blob::write_permutation(one, "MODE=0", first.data(), first.size()), "single fixture failed");
        const auto valid = one.str();
        for (size_t size = 4; size < valid.size(); ++size)
            Require(!Select(valid.substr(0, size), "MODE=0"), "truncated packed record was accepted");
        for (const size_t offset : { size_t{4}, size_t{8} })
        {
            auto corrupt = valid;
            std::fill_n(corrupt.begin() + static_cast<std::ptrdiff_t>(offset), 4, char(-1));
            Require(!Select(corrupt, "MODE=0"), "oversized packed record was accepted");
        }
        auto zeroPayload = valid;
        std::fill_n(zeroPayload.begin() + 8, 4, '\0');
        Require(!Select(zeroPayload, "MODE=0"), "zero payload was accepted");
        std::vector<std::string> keys{ "retained" };
        const auto truncatedTail = valid + "bad";
        shader_blob::enumerate_permutations(truncatedTail.data(), truncatedTail.size(), keys);
        Require(keys == std::vector<std::string>{ "retained" }, "malformed enumeration published partial results");
    }

    void CheckBuilder(const fs::path& builder, const fs::path& root)
    {
        const auto source = root / "source/main.hlsl", includes = root / "include";
        Write(source, "// #include \"ignored.hlsli\"\n/* #include \"ignored2.hlsli\" */\n"
            "#include \\\n \"local.hlsli\"\n#if 0\n#include \"conditional.hlsli\"\n#endif\n");
        Write(root / "source/local.hlsli", "#include <nested.hlsli>\n");
        Write(root / "source/conditional.hlsli", "// retained conservative dependency\n");
        Write(includes / "nested.hlsli", "#include \"cycle.hlsli\"\n");
        Write(includes / "cycle.hlsli", "#include \"nested.hlsli\"\n");
        const auto object = root / "object.dxil", depfile = root / "object.d";
        const std::vector<std::string> scan = { "--scan-dependencies", "--source", source.string(),
            "--target", object.string(), "--depfile", depfile.string(),
            "--include-directory", includes.string() };
        Require(Run(builder, scan), "recursive dependency scan failed");
        const auto dependencies = Read(depfile);
        for (const char* name : { "main.hlsl", "local.hlsli", "conditional.hlsli", "nested.hlsli", "cycle.hlsli" })
            Require(dependencies.find(name) != std::string::npos, "missing transitive shader dependency");
        Require(dependencies.find("ignored") == std::string::npos, "comment became a dependency");
        const auto oldTime = fs::file_time_type::clock::now() - std::chrono::hours(24);
        fs::last_write_time(depfile, oldTime);
        const auto depTime = fs::last_write_time(depfile);
        Require(Run(builder, scan) && fs::last_write_time(depfile) == depTime, "unchanged scan rewrote depfile");

        Write(object, "first");
        Write(root / "second.dxil", "second");
        const auto catalog = root / "catalog.txt", output = root / "family.bin";
        const auto firstRow = "MODE=0\t" + object.generic_string() + "\n";
        const auto secondRow = "MODE=1\t" + (root / "second.dxil").generic_string() + "\n";
        const std::vector<std::string> pack = { "--output", output.string(), "--catalog", catalog.string() };
        Write(catalog, firstRow + secondRow);
        Require(Run(builder, pack), "builder could not pack catalog");
        const auto bytes = Read(output);
        Require(Select(bytes, "MODE=0") == "first" && Select(bytes, "MODE=1") == "second",
            "builder packed the wrong permutation bytes");
        fs::last_write_time(output, oldTime);
        const auto outputTime = fs::last_write_time(output);
        Write(source, Read(source) + "// changed comment\n");
        Require(Run(builder, scan) && Run(builder, pack) && Read(output) == bytes &&
            fs::last_write_time(output) == outputTime, "equivalent compilation changed blob identity");
        Write(root / "empty.dxil", "");
        for (const auto& invalid : { std::string{}, std::string{"bad row\n"}, firstRow + firstRow,
                "\t" + object.generic_string() + "\n" + secondRow,
                "MODE=0\t" + (root / "missing.dxil").generic_string() + "\n",
                "MODE=0\t" + (root / "empty.dxil").generic_string() + "\n" })
        {
            Write(catalog, invalid);
            Require(!Run(builder, pack) && Read(output) == bytes &&
                fs::last_write_time(output) == outputTime, "failed pack replaced the published blob");
        }
        Write(catalog, "\t" + object.generic_string() + "\n");
        Require(Run(builder, pack) && Read(output) == Read(object), "default family was not raw DXIL");
        Require(!Run(builder, { "--output" }) && !Run(builder, { "--unknown", "value" }),
            "builder accepted a malformed invocation");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 7, "expected inventory, framework, application, runtime, builder and scratch paths");
        const fs::path scratch = fs::absolute(argv[6]) /
            ("shader-suite-" + std::to_string(_getpid()) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        CheckBundle(argv[1], argv[2], argv[3], argv[4]);
        CheckFactory(scratch / "factory");
        CheckBuilder(argv[5], scratch / "builder");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "shader contract failed: " << error.what() << '\n';
        return 1;
    }
}
