#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    [[nodiscard]] std::string ReadText(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot read " + path.string());
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
    }

    void WriteText(const fs::path& path, std::string_view text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
            throw std::runtime_error("cannot write self-test fixture");
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream)
            throw std::runtime_error("cannot finish self-test fixture");
    }

    [[nodiscard]] std::size_t CountOccurrences(
        std::string_view haystack,
        std::string_view needle)
    {
        std::size_t count = 0u;
        std::size_t position = 0u;
        while ((position = haystack.find(needle, position)) !=
            std::string_view::npos)
        {
            ++count;
            position += needle.size();
        }
        return count;
    }

    [[nodiscard]] std::string BuildFileText(const fs::path& root)
    {
        std::string combined = ReadText(root / "CMakeLists.txt");
        const fs::path cmake = root / "cmake";
        if (fs::is_directory(cmake))
        {
            std::vector<fs::path> scripts;
            for (const fs::directory_entry& entry :
                fs::recursive_directory_iterator(cmake))
            {
                if (entry.is_regular_file() &&
                    entry.path().extension() == ".cmake")
                {
                    scripts.push_back(entry.path());
                }
            }
            std::sort(scripts.begin(), scripts.end());
            for (const fs::path& script : scripts)
            {
                combined.push_back('\n');
                combined += ReadText(script);
            }
        }
        return combined;
    }

    void ValidatePatch(const fs::path& path)
    {
        const std::string text = ReadText(path);
        std::istringstream lines(text);
        std::string line;
        std::size_t files = 0u;
        constexpr std::string_view Prefix = "diff --git a/";
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.compare(0u, Prefix.size(), Prefix) != 0)
                continue;
            const std::size_t separator = line.find(" b/", Prefix.size());
            if (separator == std::string::npos)
                throw std::runtime_error("malformed diff header in " + path.string());
            const std::string before = line.substr(
                Prefix.size(), separator - Prefix.size());
            const std::string after = line.substr(separator + 3u);
            if (before.empty() || before != after)
            {
                throw std::runtime_error(
                    "renamed override paths are unsupported in " +
                    path.string());
            }
            ++files;
        }
        if (files == 0u)
            throw std::runtime_error("override patch has no file diff: " + path.string());
    }

    void ValidateInventory(const fs::path& root)
    {
        const fs::path overrides = root / "overrides";
        if (!fs::is_directory(overrides))
            throw std::runtime_error("overrides directory is missing");

        const std::string buildFiles = BuildFileText(root);
        std::set<std::string> discovered;
        for (const fs::directory_entry& entry :
            fs::recursive_directory_iterator(overrides))
        {
            if (entry.is_symlink())
                throw std::runtime_error("override symlink is forbidden");
            if (!entry.is_regular_file())
                continue;
            const std::string relative = fs::relative(entry.path(), root)
                .generic_string();
            if (!discovered.insert(relative).second)
                throw std::runtime_error("duplicate override path");
            if (entry.path().extension() == ".patch")
                ValidatePatch(entry.path());
            else if (entry.file_size() == 0u)
                throw std::runtime_error("empty override file: " + relative);
        }
        if (discovered.empty())
            throw std::runtime_error("override inventory is empty");

        for (const std::string& relative : discovered)
        {
            const std::size_t references = CountOccurrences(
                buildFiles, relative);
            if (references != 1u)
            {
                throw std::runtime_error(
                    relative + " has " + std::to_string(references) +
                    " active build references; expected one");
            }
        }

        constexpr std::string_view Prefix = "overrides/";
        std::size_t position = 0u;
        while ((position = buildFiles.find(Prefix, position)) !=
            std::string::npos)
        {
            std::size_t end = position + Prefix.size();
            while (end < buildFiles.size())
            {
                const unsigned char value = buildFiles[end];
                const bool accepted =
                    (value >= 'a' && value <= 'z') ||
                    (value >= 'A' && value <= 'Z') ||
                    (value >= '0' && value <= '9') ||
                    value == '/' || value == '-' || value == '_' ||
                    value == '.';
                if (!accepted)
                    break;
                ++end;
            }
            const std::string referenced = buildFiles.substr(
                position, end - position);
            if (discovered.count(referenced) == 0u)
            {
                throw std::runtime_error(
                    "build references untracked override " + referenced);
            }
            position = end;
        }
    }

    void RequireFailure(const fs::path& root)
    {
        try
        {
            ValidateInventory(root);
        }
        catch (const std::exception&)
        {
            return;
        }
        throw std::runtime_error("invalid self-test fixture was accepted");
    }

    void SelfTest()
    {
        const fs::path root = fs::temp_directory_path() /
            ("uvsr-override-inventory-" + std::to_string(
                static_cast<unsigned long long>(std::rand())));
        fs::remove_all(root);
        try
        {
            WriteText(root / "overrides/example.patch",
                "diff --git a/example.cpp b/example.cpp\n"
                "--- a/example.cpp\n+++ b/example.cpp\n@@ -1 +1 @@\n-a\n+b\n");
            WriteText(root / "CMakeLists.txt",
                "set(patch overrides/example.patch)\n");
            ValidateInventory(root);

            WriteText(root / "CMakeLists.txt", "project(test)\n");
            RequireFailure(root);
            WriteText(root / "CMakeLists.txt",
                "set(a overrides/example.patch)\n"
                "set(b overrides/example.patch)\n");
            RequireFailure(root);
            WriteText(root / "overrides/unused.h", "#pragma once\n");
            RequireFailure(root);
        }
        catch (...)
        {
            fs::remove_all(root);
            throw;
        }
        fs::remove_all(root);
    }
}

int main(int argumentCount, char** arguments)
{
    try
    {
        if (argumentCount == 2 &&
            std::string_view(arguments[1]) == "--self-test")
        {
            SelfTest();
            std::cout << "override inventory self-test passed\n";
            return EXIT_SUCCESS;
        }
        if (argumentCount == 3 &&
            std::string_view(arguments[1]) == "--check")
        {
            ValidateInventory(fs::weakly_canonical(arguments[2]));
            std::cout << "override inventory passed\n";
            return EXIT_SUCCESS;
        }
        std::cerr << "usage: uvsr_override_inventory_validator "
            "--check <repository-root> | --self-test\n";
        return EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        std::cerr << "override inventory failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
