#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "renderer_shader_factory_nvrhi.h"
#include "shader_blob_fixture.h"

#include <process.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace uvsr
{
    struct RendererShaderFactoryTestAccess
    {
        static std::optional<std::string> Select(RendererShaderFactory& factory,
            const char* file, const char* entry, ArrayView<const shader_blob::Constant> macros = {})
        {
            const void* bytes = nullptr;
            size_t size = 0;
            if (!factory.SelectBytecode(file, entry, macros, bytes, size)) return std::nullopt;
            return std::string(static_cast<const char*>(bytes), size);
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
        if (!condition)
        {
            std::cerr << "shader contract failed: " << message << '\n';
            std::exit(1);
        }
    }
    std::string Read(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(bool(stream), "cannot read " + path.u8string());
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }
    void Write(const fs::path& path, const std::string& value)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(value.data(), static_cast<std::streamsize>(value.size()));
        Require(bool(stream), "cannot write fixture " + path.u8string());
    }
    void AppendArgument(std::wstring& command, const std::wstring& argument)
    {
        if (!command.empty()) command += L' ';
        command += L'"';
        size_t slashes = 0;
        for (wchar_t character : argument)
        {
            if (character == L'\\') { ++slashes; continue; }
            command.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            slashes = 0;
            command += character;
        }
        command.append(slashes * 2, L'\\');
        command += L'"';
    }
    bool Run(const fs::path& executable, const std::vector<std::string>& arguments,
        const fs::path& diagnosticPath = {}, const fs::path& occupiedOutput = {})
    {
        std::wstring command;
        AppendArgument(command, executable.native());
        for (const auto& argument : arguments) AppendArgument(command, fs::u8path(argument).native());
        Require(command.size() < 32767, "builder fixture command line is too large");
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        const HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, 0, nullptr);
        Require(input != INVALID_HANDLE_VALUE, "cannot open builder fixture input");
        const bool capture = !diagnosticPath.empty();
        const HANDLE diagnostic = capture ? CreateFileW(diagnosticPath.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) : GetStdHandle(STD_ERROR_HANDLE);
        Require(diagnostic != INVALID_HANDLE_VALUE && diagnostic, "cannot open builder fixture diagnostics");
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input;
        startup.hStdOutput = capture ? diagnostic : GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = diagnostic;
        PROCESS_INFORMATION process{};
        const DWORD flags = CREATE_NO_WINDOW | (occupiedOutput.empty() ? 0 : CREATE_SUSPENDED);
        const bool started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
            TRUE, flags, nullptr, nullptr, &startup, &process) != FALSE;
        DWORD wait = WAIT_FAILED;
        DWORD status = 1;
        bool readStatus = false;
        bool reserved = occupiedOutput.empty();
        bool reservationIntact = true;
        std::wstring reservation;
        if (started)
        {
            if (!occupiedOutput.empty())
            {
                reservation = occupiedOutput.native() + L".tmp-" + std::to_wstring(process.dwProcessId);
                const HANDLE file = CreateFileW(reservation.c_str(), GENERIC_WRITE, 0,
                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                reserved = file != INVALID_HANDLE_VALUE;
                if (reserved) CloseHandle(file);
                if (reserved) ResumeThread(process.hThread);
                else TerminateProcess(process.hProcess, 1);
            }
            CloseHandle(process.hThread);
            wait = WaitForSingleObject(process.hProcess, 30000);
            if (wait != WAIT_OBJECT_0)
            {
                TerminateProcess(process.hProcess, 1);
                WaitForSingleObject(process.hProcess, 5000);
            }
            readStatus = GetExitCodeProcess(process.hProcess, &status) != FALSE;
            CloseHandle(process.hProcess);
            if (reserved && !reservation.empty())
            {
                reservationIntact = GetFileAttributesW(reservation.c_str()) != INVALID_FILE_ATTRIBUTES;
                if (reservationIntact) DeleteFileW(reservation.c_str());
            }
        }
        CloseHandle(input);
        if (capture) CloseHandle(diagnostic);
        Require(started && wait == WAIT_OBJECT_0 && readStatus, "builder fixture child did not complete");
        Require(reserved && reservationIntact, "builder deleted a temporary name owned by another writer");
        return status == 0;
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
                const fs::path object = fs::u8path(line.substr(tab + 1));
                auto relative = fs::relative(object, root / "objects");
                const auto stem = relative.stem().u8string();
                const auto hash = stem.rfind('.');
                Require(hash != std::string::npos, "compiled shader lacks task identity");
                relative.replace_filename(stem.substr(0, hash));
                catalog[relative.generic_u8string()].push_back({ line.substr(0, tab), object });
            }
        }
        Require(!catalog.empty(), "compiled shader catalog is empty");
        return catalog;
    }

    void CheckBundle(const fs::path& inventory, const fs::path& application,
        const fs::path& runtime)
    {
        const auto appCatalog = ReadCatalog(application);
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
            { "renderer_skinning_cs", 1 },
            { "renderer_imgui_vertex", 1 },
            { "renderer_imgui_pixel", 1 },
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
            Require(path.extension() == ".bin" && path.lexically_normal().generic_u8string() == relative,
                "unsafe shader inventory path");
            for (const auto& part : path)
                Require(part != ".." && part != ".", "shader inventory escapes its root");
            Require(relative.rfind("uvsr/dxil/", 0) == 0, "unknown shader owner");
            const std::string family = relative.substr(10, relative.size() - 14);
            Require(appCatalog.count(family) != 0, "staged family has no compiled catalog: " + family);
            const auto bytes = Read(application / "dxil" / (family + ".bin"));
            Require(!bytes.empty() && bytes == Read(runtime / path),
                "runtime shader differs from compiled family: " + relative);
            std::set<std::string> keys;
            for (const auto& row : appCatalog.at(family))
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
            packagedAppFamilies.insert(family);
        }
        Require(expected.size() == 27 && packagedAppFamilies.size() == retained.size(),
            "package omitted a retained shader family");
        for (const auto& entry : fs::recursive_directory_iterator(runtime))
        {
            Require(!entry.is_symlink(), "shader package contains a link");
            if (entry.is_directory()) continue;
            Require(entry.is_regular_file(), "shader package contains a non-file");
            actual.insert(fs::relative(entry.path(), runtime).generic_u8string());
        }
        Require(actual == expected, "runtime shader tree differs from exact inventory");
    }

    void CheckFactory(const fs::path& root)
    {
        RendererShaderFactory factory(nullptr, root.c_str());
        for (const char* file : { "", "framework/a.hlsl", "uvsr/../a.hlsl", "uvsr/./a.hlsl",
                "uvsr\\a.hlsl", "C:/a.hlsl", "uvsr/a.bin", "uvsr/passes/a.hlsl", "uvsr/C:a.hlsl" })
            Require(!Access::Select(factory, file, "main"), "factory accepted unsafe shader path");
        for (const char* entry : { "", "../entry" })
            Require(!Access::Select(factory, "uvsr/a.hlsl", entry), "factory accepted unsafe entry");
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
        shader_blob::Constant macros[] = { { "BETA", "2" }, { "ALPHA", "1" } };
        Require(Access::Select(factory, "uvsr/packed.hlsl", "main", macros) == first &&
            !Access::Select(factory, "uvsr/cache.hlsl", "main", macros), "factory macro selection changed");
        macros[0].value = "9";
        Require(!Access::Select(factory, "uvsr/packed.hlsl", "main", macros),
            "factory accepted a missing permutation");
        Require(!factory.CreateShader("uvsr/cache.hlsl", "main", {}, nvrhi::ShaderType::Compute),
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
        const std::vector<std::string> scan = { "--scan-dependencies", "--source", source.u8string(),
            "--target", object.u8string(), "--depfile", depfile.u8string(),
            "--include-directory", includes.u8string() };
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
        const auto firstRow = "MODE=0\t" + object.generic_u8string() + "\n";
        const auto secondRow = "MODE=1\t" + (root / "second.dxil").generic_u8string() + "\n";
        const std::vector<std::string> pack = { "--output", output.u8string(), "--catalog", catalog.u8string() };
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
                "\t" + object.generic_u8string() + "\n" + secondRow,
                "MODE=0\t" + (root / "missing.dxil").generic_u8string() + "\n",
                "MODE=0\t" + (root / "empty.dxil").generic_u8string() + "\n" })
        {
            Write(catalog, invalid);
            Require(!Run(builder, pack) && Read(output) == bytes &&
                fs::last_write_time(output) == outputTime, "failed pack replaced the published blob");
        }
        Write(catalog, "\t" + object.generic_u8string() + "\n");
        Require(Run(builder, pack) && Read(output) == Read(object), "default family was not raw DXIL");
        Require(!Run(builder, { "--output" }) && !Run(builder, { "--unknown", "value" }),
            "builder accepted a malformed invocation");
    }

    void CheckNoTemporary(const fs::path& output)
    {
        const auto prefix = output.filename().u8string() + ".tmp-";
        for (const auto& item : fs::directory_iterator(output.parent_path()))
            Require(item.path().filename().u8string().rfind(prefix, 0) != 0,
                "builder retained an owned temporary after failure");
    }

    void CheckAllocationFailures(const fs::path& builder, const std::vector<std::string>& arguments,
        const fs::path& output, const fs::path& diagnostic)
    {
        Require(Run(builder, arguments, diagnostic), "allocation fixture baseline failed");
        const auto expected = Read(output);
        const auto oldTime = fs::file_time_type::clock::now() - std::chrono::hours(24);
        fs::last_write_time(output, oldTime);
        const auto stamp = fs::last_write_time(output);
        unsigned completed = 0;
        for (unsigned failure = 1; failure <= 512; ++failure)
        {
            const auto value = std::to_string(failure);
            Require(SetEnvironmentVariableA("UVSR_SHADER_TOOL_FAIL_ALLOCATION", value.c_str()) != FALSE,
                "cannot set allocation fixture");
            const bool succeeded = Run(builder, arguments, diagnostic);
            Require(SetEnvironmentVariableA("UVSR_SHADER_TOOL_FAIL_ALLOCATION", nullptr) != FALSE,
                "cannot clear allocation fixture");
            Require(Read(output) == expected && fs::last_write_time(output) == stamp,
                "allocation failure changed the previous output or its timestamp");
            CheckNoTemporary(output);
            if (succeeded) { completed = failure; break; }
            Require(Read(diagnostic).find("out of memory") != std::string::npos,
                "allocation failure lost its readable diagnostic");
        }
        Require(completed > 1, "allocation fixture did not cover failures and a successful retry");
        std::cout << "shader builder allocation failures verified: " << completed - 1 << '\n';
    }

    void CheckBuilderFailures(const fs::path& builder, const fs::path& root)
    {
        const auto diagnostic = root / "diagnostic.txt";
        const auto object = root / "object.dxil";
        const auto catalog = root / "catalog.txt";
        const auto output = root / "family.bin";
        const std::vector<std::string> pack{"--output", output.u8string(), "--catalog", catalog.u8string()};
        Write(object, "abcdefghijklmnopqrstuv");
        Write(catalog, "\t" + object.generic_u8string() + "\n");
        const auto header = root / "family.h";
        auto headerArguments = pack;
        headerArguments[1] = header.u8string();
        headerArguments.insert(headerArguments.end(), {"--header-symbol", "KnownFixture"});
        Require(Run(builder, headerArguments, diagnostic), "header fixture failed");
        const std::string expectedHeader = "// Generated by uvsr_shader_blob_builder.\n"
            "const uint8_t KnownFixture[] = {\n"
            "    97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116,\n"
            "    117, 118\n};\n";
        Require(Read(header) == expectedHeader, "streamed header array differs from the known byte format");
        headerArguments.back() = "invalid-symbol";
        Require(!Run(builder, headerArguments, diagnostic) && Read(header) == expectedHeader &&
            Read(diagnostic).find("invalid shader header symbol") != std::string::npos,
            "invalid header symbol changed the previous header or lost its diagnostic");

        CheckAllocationFailures(builder, pack, output, diagnostic);
        const auto original = Read(output);
        const auto stamp = fs::last_write_time(output);
        Write(object, "changed bytes requiring replacement");
        Require(!Run(builder, pack, diagnostic, output) && Read(output) == original &&
            fs::last_write_time(output) == stamp && Read(diagnostic).find("cannot create") != std::string::npos,
            "temporary name collision changed the published shader or lost its diagnostic");
        const HANDLE held = CreateFileW(output.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(held != INVALID_HANDLE_VALUE, "cannot hold published shader for the replacement failure");
        const bool published = Run(builder, pack, diagnostic);
        CloseHandle(held);
        Require(!published && Read(output) == original && fs::last_write_time(output) == stamp &&
            Read(diagnostic).find("cannot publish") != std::string::npos,
            "failed native replacement lost the previous shader or its diagnostic");
        CheckNoTemporary(output);
        Require(Run(builder, pack, diagnostic) && Read(output) == Read(object),
            "failed native replacement was not retryable");

        const auto unicode = root / fs::u8path(u8"unicode #$ \u03bb");
        const auto source = unicode / "main.hlsl";
        const auto child = unicode / fs::u8path(u8"child #$ \u03bb.hlsli");
        const auto depfile = unicode / "object.d";
        const std::vector<std::string> scan{"--scan-dependencies", "--source", source.u8string(),
            "--target", (unicode / "object #$.dxil").u8string(), "--depfile", depfile.u8string()};
        Write(source, u8"#include \"child #$ \u03bb.hlsli\"\n");
        Write(child, "#include \"main.hlsl\"\n");
        Require(Run(builder, scan, diagnostic), "Unicode dependency fixture failed");
        const auto dependencies = Read(depfile);
        Require(dependencies.find(u8"\u03bb") != std::string::npos &&
            dependencies.find("\\#$$") != std::string::npos &&
            std::count(dependencies.begin(), dependencies.end(), '\n') == 3,
            "Unicode paths, depfile escaping or cycle suppression changed");
        CheckAllocationFailures(builder, scan, depfile, diagnostic);
        const auto depStamp = fs::last_write_time(depfile);
        for (const auto& invalid : {std::string("#include VARIABLE\n"), std::string("#include \"\"\n"),
                std::string("#include <unfinished\n"), std::string("#include < >\n#include VARIABLE\n")})
        {
            Write(source, invalid);
            Require(!Run(builder, scan, diagnostic) && Read(depfile) == dependencies &&
                fs::last_write_time(depfile) == depStamp &&
                Read(diagnostic).find(source.generic_u8string()) != std::string::npos,
                "invalid include changed the published dependency file or lost its path");
            CheckNoTemporary(depfile);
        }
        Write(source, "#include \"\xff.hlsli\"\n");
        Require(!Run(builder, scan, diagnostic) && Read(depfile) == dependencies &&
            Read(diagnostic).find("invalid UTF-8 path") != std::string::npos,
            "invalid UTF-8 include was accepted or lost its diagnostic");
        Write(source, "#include \"unresolved_native_header.h\"\n");
        Require(Run(builder, scan, diagnostic) && Read(depfile).find("unresolved_native_header") == std::string::npos,
            "the scanner replaced DXC's unresolved-include authority");

        constexpr unsigned Depth = 8192;
        const auto deep = root / "deep";
        for (unsigned index = 0; index < Depth; ++index)
        {
            const auto next = (index + 1) % Depth;
            Write(deep / ("part-" + std::to_string(index) + ".hlsli"),
                "#include \"part-" + std::to_string(next) + ".hlsli\"\n");
        }
        const auto deepDepfile = deep / "deep.d";
        Require(Run(builder, {"--scan-dependencies", "--source", (deep / "part-0.hlsli").u8string(),
            "--target", (deep / "deep.dxil").u8string(), "--depfile", deepDepfile.u8string()}, diagnostic),
            "the deep include chain did not finish iteratively");
        const auto deepText = Read(deepDepfile);
        Require(std::count(deepText.begin(), deepText.end(), '\n') == Depth + 1 &&
            deepText.find("part-8191.hlsli") != std::string::npos,
            "the deep dependency walk omitted nodes or repeated a cycle");
    }
}

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetEnvironmentVariableA("UVSR_SHADER_TOOL_FAIL_ALLOCATION", nullptr);
    Require(argc == 6, "expected inventory, application, runtime, builder and scratch paths");
    const fs::path scratch = fs::absolute(argv[5]) /
        ("shader-suite-" + std::to_string(_getpid()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CheckBundle(argv[1], argv[2], argv[3]);
    CheckFactory(scratch / "factory");
    CheckBuilder(argv[4], scratch / "builder");
    CheckBuilderFailures(argv[4], scratch / "builder-failures");
    return 0;
}
