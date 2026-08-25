#include "renderer_shader_factory.h"

#include "shader_blob.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace uvsr
{
    struct RendererShaderFactoryTestAccess
    {
        static std::optional<std::filesystem::path> Resolve(
            const std::filesystem::path& directory,
            const char* fileName,
            const char* entryName)
        {
            return RendererShaderFactory::ResolveBlobPath(
                directory, fileName, entryName);
        }

        static std::optional<std::vector<uint8_t>> Select(
            RendererShaderFactory& factory,
            const char* fileName,
            const char* entryName,
            const std::vector<RendererShaderMacro>* defines)
        {
            const auto selected = factory.SelectBytecode(
                fileName, entryName, defines);
            if (!selected)
                return std::nullopt;
            const auto* first = static_cast<const uint8_t*>(selected->data);
            return std::vector<uint8_t>(first, first + selected->size);
        }
    };
}

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Renderer shader factory test failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void WriteRaw(
        const std::filesystem::path& path,
        const std::vector<uint8_t>& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        Require(output.good(), "could not create a raw shader blob");
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        Require(output.good(), "could not write a raw shader blob");
    }

    void WriteEmpty(const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        Require(output.good(), "could not create an empty shader blob");
    }

    void WritePacked(const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        const std::vector<uint8_t> first = { 0x10u, 0x20u, 0x30u };
        const std::vector<uint8_t> second = { 0x40u, 0x50u };
        Require(
            output.good() && uvsr::shader_blob::write_header(output) &&
                uvsr::shader_blob::write_permutation(
                    output, "ALPHA=1 BETA=2", first.data(), first.size()) &&
                uvsr::shader_blob::write_permutation(
                    output, "ALPHA=2 BETA=1", second.data(), second.size()),
            "could not write a packed shader blob");
    }

    void PathKnownAnswers(const std::filesystem::path& directory)
    {
        using uvsr::RendererShaderFactoryTestAccess;
        const auto mainPath = RendererShaderFactoryTestAccess::Resolve(
            directory, "uvsr/path_tracing_cs.hlsl", "main");
        Require(
            mainPath == directory / "path_tracing_cs.bin",
            "main entry path changed");
        const auto namedPath = RendererShaderFactoryTestAccess::Resolve(
            directory,
            "uvsr/ray_traced_sky_visibility_cs.hlsl",
            "Generate");
        Require(
            namedPath == directory /
                "ray_traced_sky_visibility_cs_Generate.bin",
            "named entry path changed");
        constexpr const char* invalidFiles[] = {
            "",
            "framework/example.hlsl",
            "uvsr/../example.hlsl",
            "uvsr/./example.hlsl",
            "uvsr\\example.hlsl",
            "C:/example.hlsl",
            "uvsr/example.bin",
            "uvsr/passes/example.hlsl"
        };
        for (const char* fileName : invalidFiles)
        {
            Require(
                !RendererShaderFactoryTestAccess::Resolve(
                    directory, fileName, "main"),
                "unsafe or unsupported shader path was accepted");
        }
        Require(
            !RendererShaderFactoryTestAccess::Resolve(
                directory, "uvsr/example.hlsl", "../entry") &&
            !RendererShaderFactoryTestAccess::Resolve(
                directory, "uvsr/example.hlsl", ""),
            "unsafe shader entry was accepted");
    }

    void BlobSelectionKnownAnswers(const std::filesystem::path& directory)
    {
        using namespace uvsr;
        RendererShaderFactory factory(nullptr, directory);

        const std::vector<uint8_t> raw = { 0x44u, 0x58u, 0x49u, 0x4cu };
        WriteRaw(directory / "raw.bin", raw);
        Require(
            RendererShaderFactoryTestAccess::Select(
                factory, "uvsr/raw.hlsl", "main", nullptr) == raw,
            "raw DXIL selection changed");

        WritePacked(directory / "permuted.bin");
        const std::vector<RendererShaderMacro> reversedDefines = {
            { "BETA", "2" },
            { "ALPHA", "1" }
        };
        const std::vector<uint8_t> expected = { 0x10u, 0x20u, 0x30u };
        Require(
            RendererShaderFactoryTestAccess::Select(
                factory,
                "uvsr/permuted.hlsl",
                "main",
                &reversedDefines) == expected,
            "sorted permutation selection changed");
        const std::vector<RendererShaderMacro> missingDefines = {
            { "ALPHA", "9" },
            { "BETA", "9" }
        };
        Require(
            !RendererShaderFactoryTestAccess::Select(
                factory,
                "uvsr/permuted.hlsl",
                "main",
                &missingDefines),
            "missing permutation was accepted");
        Require(
            !RendererShaderFactoryTestAccess::Select(
                factory,
                "uvsr/raw.hlsl",
                "main",
                &reversedDefines),
            "raw DXIL accepted permutation constants");

        const std::vector<uint8_t> named = { 0xa1u, 0xb2u };
        WriteRaw(directory / "named_Generate.bin", named);
        Require(
            RendererShaderFactoryTestAccess::Select(
                factory, "uvsr/named.hlsl", "Generate", nullptr) == named,
            "named entry blob selection changed");
    }

    void CacheKnownAnswers(const std::filesystem::path& directory)
    {
        using namespace uvsr;
        RendererShaderFactory factory(nullptr, directory);
        const std::vector<uint8_t> first = { 1u, 2u };
        const std::vector<uint8_t> second = { 3u, 4u, 5u };
        WriteRaw(directory / "cached.bin", first);
        Require(
            RendererShaderFactoryTestAccess::Select(
                factory, "uvsr/cached.hlsl", "main", nullptr) == first,
            "initial cached blob selection failed");
        WriteRaw(directory / "cached.bin", second);
        Require(
            RendererShaderFactoryTestAccess::Select(
                factory, "uvsr/cached.hlsl", "main", nullptr) == first,
            "blob cache did not retain loaded bytes");
        factory.ClearCache();
        Require(
            RendererShaderFactoryTestAccess::Select(
                factory, "uvsr/cached.hlsl", "main", nullptr) == second,
            "cache clear did not reload the blob");

        WriteEmpty(directory / "retry.bin");
        Require(
            !RendererShaderFactoryTestAccess::Select(
                factory, "uvsr/retry.hlsl", "main", nullptr),
            "empty shader blob was accepted");
        WriteRaw(directory / "retry.bin", second);
        Require(
            RendererShaderFactoryTestAccess::Select(
                factory, "uvsr/retry.hlsl", "main", nullptr) == second,
            "failed load was cached instead of retried");
    }
}

int main(int argc, char** argv)
{
    Require(argc == 2, "expected one external scratch-directory argument");
    const std::filesystem::path directory =
        std::filesystem::path(argv[1]) / "uvsr-dxil";
    std::filesystem::create_directories(directory);
    PathKnownAnswers(directory);
    BlobSelectionKnownAnswers(directory);
    CacheKnownAnswers(directory);

    uvsr::RendererShaderFactory nullFactory(nullptr, directory);
    Require(
        !nullFactory.CreateShader(
            "uvsr/raw.hlsl", "main", nullptr, nvrhi::ShaderType::Compute) &&
        !nullFactory.CreateShaderLibrary("uvsr/raw.hlsl", nullptr),
        "factory without a device created an NVRHI object");
    return 0;
}
