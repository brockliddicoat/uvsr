#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Runtime shader bundle validation failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    std::vector<char> ReadBinaryFile(
        const std::filesystem::path& path,
        const char* failureMessage)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(stream.good(), failureMessage);
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
    }

    bool IsSafeRelativeShaderPath(std::string_view path)
    {
        if (path.empty() || path.front() == '/' || path.back() == '/' ||
            path.find('\\') != std::string_view::npos ||
            path.find(':') != std::string_view::npos ||
            path.find("//") != std::string_view::npos)
        {
            return false;
        }

        const std::filesystem::path filesystemPath{ path };
        if (filesystemPath.is_absolute() ||
            filesystemPath.extension() != ".bin")
        {
            return false;
        }
        for (const std::filesystem::path& component : filesystemPath)
        {
            if (component == "." || component == "..")
                return false;
        }
        return filesystemPath.lexically_normal().generic_string() == path;
    }
}

int main(int argc, char** argv)
{
    Require(argc == 3,
        "expected source-mapping manifest and runtime-root arguments");
    const std::filesystem::path sourceManifestPath = argv[1];
    const std::filesystem::path runtimeRoot = argv[2];

    std::ifstream manifest(sourceManifestPath, std::ios::binary);
    Require(manifest.good(),
        "could not open the generated source-mapping manifest");

    std::set<std::string> expected;
    std::string mapping;
    while (std::getline(manifest, mapping))
    {
        if (!mapping.empty() && mapping.back() == '\r')
            mapping.pop_back();
        if (mapping.empty())
            continue;

        const size_t separator = mapping.find('|');
        Require(separator != std::string::npos && separator > 0u &&
            mapping.find('|', separator + 1u) == std::string::npos,
            "source-mapping manifest contains a malformed entry");
        const std::filesystem::path sourcePath =
            mapping.substr(0u, separator);
        const std::string relativePath = mapping.substr(separator + 1u);
        Require(sourcePath.is_absolute(),
            "a manifested shader source is not absolute");
        Require(IsSafeRelativeShaderPath(relativePath),
            "manifest contains an unsafe runtime shader path");

        const bool inserted = expected.emplace(
            std::filesystem::path(relativePath).generic_string()).second;
        Require(inserted, "manifest contains a duplicate path");
        const std::filesystem::path shaderPath =
            runtimeRoot / std::filesystem::path(relativePath);
        Require(
            std::filesystem::is_regular_file(sourcePath),
            "a manifested compiled shader is missing");
        Require(
            std::filesystem::is_regular_file(shaderPath),
            "a manifested shader is missing");
        const std::vector<char> sourceBytes = ReadBinaryFile(
            sourcePath,
            "could not read a manifested compiled shader");
        const std::vector<char> runtimeBytes = ReadBinaryFile(
            shaderPath,
            "could not read a manifested runtime shader");
        Require(
            !sourceBytes.empty(),
            "a manifested compiled shader is empty");
        Require(
            runtimeBytes == sourceBytes,
            "a runtime shader does not exactly match its compiled source");
    }
    Require(!expected.empty(), "manifest is empty");

    std::set<std::string> actual;
    for (const std::filesystem::directory_entry& entry :
        std::filesystem::recursive_directory_iterator(runtimeRoot))
    {
        Require(!entry.is_symlink(),
            "runtime shader tree contains a symbolic link");
        if (entry.is_directory())
            continue;
        Require(entry.is_regular_file(),
            "runtime shader tree contains a non-file entry");
        const std::string relativePath = std::filesystem::relative(
            entry.path(), runtimeRoot).generic_string();
        Require(IsSafeRelativeShaderPath(relativePath),
            "runtime shader tree contains an unexpected file");
        actual.emplace(relativePath);
    }
    Require(
        actual == expected,
        "runtime shader set does not exactly match the manifest");

    return 0;
}
