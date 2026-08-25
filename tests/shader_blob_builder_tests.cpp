#include <process.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Shader blob builder test failed: "
                      << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void WriteText(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        Require(output.good(), "could not create text fixture");
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        Require(output.good(), "could not write text fixture");
    }

    void WriteBinary(
        const fs::path& path,
        const std::vector<std::uint8_t>& bytes)
    {
        fs::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        Require(output.good(), "could not create binary fixture");
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        Require(output.good(), "could not write binary fixture");
    }

    std::string ReadText(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
    }

    std::vector<std::uint8_t> ReadBinary(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
    }

    bool Run(
        const fs::path& executable,
        const std::vector<std::string>& options)
    {
        std::vector<std::string> arguments;
        arguments.reserve(options.size() + 1u);
        arguments.push_back(executable.string());
        arguments.insert(arguments.end(), options.begin(), options.end());
        std::vector<const char*> rawArguments;
        rawArguments.reserve(arguments.size() + 1u);
        for (const std::string& argument : arguments)
            rawArguments.push_back(argument.c_str());
        rawArguments.push_back(nullptr);
        return _spawnv(
            _P_WAIT,
            executable.string().c_str(),
            rawArguments.data()) == 0;
    }

    std::vector<std::string> ScanArguments(
        const fs::path& source,
        const fs::path& target,
        const fs::path& depfile,
        const fs::path& sourceDirectory,
        const fs::path& includeDirectory)
    {
        return {
            "--scan-dependencies",
            "--source", source.string(),
            "--target", target.string(),
            "--depfile", depfile.string(),
            "--include-directory", sourceDirectory.string(),
            "--include-directory", includeDirectory.string()
        };
    }
}

int main(int argc, char** argv)
{
    Require(argc == 3,
        "expected builder executable and external scratch directory");
    const fs::path builder = fs::path(argv[1]);
    const fs::path root =
        fs::path(argv[2]) / "shader-blob-builder-self-test";
    std::error_code removeError;
    fs::remove_all(root, removeError);
    Require(!removeError, "could not reset external scratch directory");

    const fs::path sourceDirectory = root / "source";
    const fs::path includeDirectory = root / "include";
    const fs::path source = sourceDirectory / "main.hlsl";
    const fs::path level1 = sourceDirectory / "local" / "level1.hlsli";
    const fs::path conditional = sourceDirectory / "conditional.hlsli";
    const fs::path level2 = includeDirectory / "shared" / "level2.hlsli";
    const fs::path level3 = includeDirectory / "shared" / "level3.hlsli";
    const fs::path object0 = root / "objects" / "family.0.dxil";
    const fs::path object1 = root / "objects" / "family.1.dxil";
    const fs::path objectDepfile = object0.string() + ".d";

    const std::string sourceBefore =
        "// #include \"ignored-line.hlsli\"\n"
        "/* #include \"ignored-block.hlsli\" */\n"
        "#include \\\n"
        "    \"local/level1.hlsli\"\n"
        "#if 0\n"
        "#include \"conditional.hlsli\"\n"
        "#endif\n"
        "[numthreads(1, 1, 1)] void main() {} // first comment\n";
    WriteText(source, sourceBefore);
    WriteText(level1,
        "#ifndef LEVEL1\n#define LEVEL1\n"
        "#include <shared/level2.hlsli>\n#endif\n");
    WriteText(conditional, "static const uint Conditional = 0u;\n");
    WriteText(level2,
        "#ifndef LEVEL2\n#define LEVEL2\n"
        "#include \"level3.hlsli\"\n#endif\n");
    WriteText(level3,
        "#ifndef LEVEL3\n#define LEVEL3\n"
        "#include <shared/level2.hlsli>\n#endif\n");

    const auto scanArguments = ScanArguments(
        source,
        object0,
        objectDepfile,
        sourceDirectory,
        includeDirectory);
    Require(Run(builder, scanArguments),
        "recursive dependency scan failed");
    const std::string dependencies = ReadText(objectDepfile);
    for (const char* required : {
        "main.hlsl", "level1.hlsli", "conditional.hlsli",
        "level2.hlsli", "level3.hlsli" })
    {
        Require(dependencies.find(required) != std::string::npos,
            "recursive or conservative dependency is missing");
    }
    Require(dependencies.find("ignored-line.hlsli") == std::string::npos &&
            dependencies.find("ignored-block.hlsli") == std::string::npos,
        "commented include became a dependency");

    const auto stableTime = fs::file_time_type::clock::now() -
        std::chrono::hours(24);
    fs::last_write_time(objectDepfile, stableTime);
    const auto depfileTime = fs::last_write_time(objectDepfile);
    Require(Run(builder, scanArguments) &&
            fs::last_write_time(objectDepfile) == depfileTime,
        "no-op dependency scan changed depfile identity");

    const std::vector<std::uint8_t> object0Bytes = {
        0x44u, 0x58u, 0x49u, 0x4cu, 0x00u
    };
    const std::vector<std::uint8_t> object1Bytes = {
        0x44u, 0x58u, 0x49u, 0x4cu, 0x01u
    };
    WriteBinary(object0, object0Bytes);
    WriteBinary(object1, object1Bytes);
    const fs::path catalog = root / "catalog.txt";
    WriteText(catalog,
        "MODE=0\t" + object0.generic_string() + "\t" +
            objectDepfile.generic_string() + "\n" +
        "MODE=1\t" + object1.generic_string() + "\t" +
            objectDepfile.generic_string() + "\n");
    const fs::path family = root / "family.bin";
    const fs::path familyDepfile = root / "family.bin.d";
    const std::vector<std::string> packArguments = {
        "--output", family.string(),
        "--depfile", familyDepfile.string(),
        "--catalog", catalog.string(),
        "--working-directory", sourceDirectory.string()
    };
    Require(Run(builder, packArguments), "initial family pack failed");
    const std::vector<std::uint8_t> familyBytes = ReadBinary(family);
    Require(!familyBytes.empty(), "packed family is empty");
    fs::last_write_time(family, stableTime);
    const auto familyTime = fs::last_write_time(family);

    WriteText(source,
        sourceBefore.substr(0, sourceBefore.find("first comment")) +
            "replacement comment\n");
    Require(Run(builder, scanArguments),
        "dependency rescan after comment edit failed");
    WriteBinary(object0, object0Bytes);
    Require(Run(builder, packArguments),
        "comment-identical family repack failed");
    Require(ReadBinary(family) == familyBytes &&
            fs::last_write_time(family) == familyTime,
        "comment-identical shader output changed family identity");
    return EXIT_SUCCESS;
}
