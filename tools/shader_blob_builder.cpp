#include "shader_blob.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <process.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
fs::path temporary_path(const fs::path& destination)
{
    return destination.string() + ".tmp-" + std::to_string(_getpid());
}

struct Entry
{
    std::string key;
    fs::path object;
    fs::path dependencies;
};

std::string read_text(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), {});
}

struct IncludeDirective
{
    std::string path;
    bool quoted = false;
};

std::string strip_comments(std::string_view source)
{
    enum class State
    {
        Code,
        LineComment,
        BlockComment,
        String,
        Character
    };

    State state = State::Code;
    std::string result;
    result.reserve(source.size());
    for (size_t index = 0; index < source.size(); ++index)
    {
        const char character = source[index];
        const char next = index + 1u < source.size()
            ? source[index + 1u]
            : '\0';
        if (state == State::LineComment)
        {
            if (character == '\n')
            {
                result.push_back(character);
                state = State::Code;
            }
            else
                result.push_back(' ');
            continue;
        }
        if (state == State::BlockComment)
        {
            if (character == '*' && next == '/')
            {
                result.append("  ");
                ++index;
                state = State::Code;
            }
            else
                result.push_back(character == '\n' ? '\n' : ' ');
            continue;
        }
        if (state == State::String || state == State::Character)
        {
            result.push_back(character);
            if (character == '\\' && next != '\0')
            {
                result.push_back(next);
                ++index;
            }
            else if ((state == State::String && character == '"') ||
                     (state == State::Character && character == '\''))
                state = State::Code;
            continue;
        }
        if (character == '/' && next == '/')
        {
            result.append("  ");
            ++index;
            state = State::LineComment;
        }
        else if (character == '/' && next == '*')
        {
            result.append("  ");
            ++index;
            state = State::BlockComment;
        }
        else
        {
            result.push_back(character);
            if (character == '"')
                state = State::String;
            else if (character == '\'')
                state = State::Character;
        }
    }
    return result;
}

std::string collapse_line_continuations(std::string source)
{
    std::string result;
    result.reserve(source.size());
    for (size_t index = 0; index < source.size(); ++index)
    {
        if (source[index] == '\\' && index + 1u < source.size() &&
            source[index + 1u] == '\n')
        {
            result.push_back(' ');
            ++index;
        }
        else if (source[index] == '\\' && index + 2u < source.size() &&
                 source[index + 1u] == '\r' &&
                 source[index + 2u] == '\n')
        {
            result.push_back(' ');
            index += 2u;
        }
        else
            result.push_back(source[index]);
    }
    return result;
}

std::vector<IncludeDirective> parse_includes(
    const fs::path& sourcePath,
    const std::string& source)
{
    std::istringstream lines(
        strip_comments(collapse_line_continuations(source)));
    std::vector<IncludeDirective> includes;
    for (std::string line; std::getline(lines, line);)
    {
        size_t position = 0u;
        if (line.size() >= 3u &&
            static_cast<unsigned char>(line[0]) == 0xefu &&
            static_cast<unsigned char>(line[1]) == 0xbbu &&
            static_cast<unsigned char>(line[2]) == 0xbfu)
            position = 3u;
        while (position < line.size() &&
            std::isspace(static_cast<unsigned char>(line[position])))
            ++position;
        if (position == line.size() || line[position] != '#')
            continue;
        ++position;
        while (position < line.size() &&
            std::isspace(static_cast<unsigned char>(line[position])))
            ++position;
        constexpr std::string_view keyword = "include";
        if (line.compare(
                position,
                keyword.size(),
                keyword.data(),
                keyword.size()) != 0)
            continue;
        position += keyword.size();
        if (position < line.size() &&
            (std::isalnum(static_cast<unsigned char>(line[position])) ||
                line[position] == '_'))
            continue;
        while (position < line.size() &&
            std::isspace(static_cast<unsigned char>(line[position])))
            ++position;
        if (position == line.size() ||
            (line[position] != '"' && line[position] != '<'))
        {
            throw std::runtime_error(
                "non-literal shader include in " + sourcePath.string());
        }
        const bool quoted = line[position] == '"';
        const char closing = quoted ? '"' : '>';
        const size_t end = line.find(closing, position + 1u);
        if (end == std::string::npos || end == position + 1u)
            throw std::runtime_error(
                "malformed shader include in " + sourcePath.string());
        includes.push_back({
            line.substr(position + 1u, end - position - 1u),
            quoted
        });
    }
    return includes;
}

fs::path normalized_existing_path(const fs::path& path)
{
    std::error_code error;
    fs::path normalized = fs::weakly_canonical(path, error);
    if (error)
    {
        error.clear();
        normalized = fs::absolute(path, error).lexically_normal();
        if (error)
            normalized = path.lexically_normal();
    }
    return normalized;
}

std::optional<fs::path> resolve_include(
    const IncludeDirective& include,
    const fs::path& includingFile,
    const std::vector<fs::path>& includeDirectories)
{
    const fs::path requested = fs::u8path(include.path);
    std::vector<fs::path> candidates;
    if (requested.is_absolute())
        candidates.push_back(requested);
    else
    {
        if (include.quoted)
            candidates.push_back(includingFile.parent_path() / requested);
        for (const fs::path& directory : includeDirectories)
            candidates.push_back(directory / requested);
    }
    for (const fs::path& candidate : candidates)
    {
        std::error_code error;
        if (fs::is_regular_file(candidate, error) && !error)
            return normalized_existing_path(candidate);
    }
    return std::nullopt;
}

void scan_shader_file(
    const fs::path& source,
    const std::vector<fs::path>& includeDirectories,
    std::set<std::string>& dependencies)
{
    const fs::path normalized = normalized_existing_path(source);
    const std::string key = normalized.generic_string();
    if (!dependencies.insert(key).second)
        return;
    const std::string text = read_text(normalized);
    for (const IncludeDirective& include : parse_includes(normalized, text))
    {
        const std::optional<fs::path> resolved = resolve_include(
            include, normalized, includeDirectories);
        // Dual C++/HLSL headers contain inactive standard-library includes.
        // DXC remains authoritative for unresolved active directives; a
        // failed compile leaves the object missing and the command retryable.
        if (resolved)
            scan_shader_file(*resolved, includeDirectories, dependencies);
    }
}

std::vector<char> read_binary(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("cannot read " + path.string());
    const std::streamoff size = input.tellg();
    if (size <= 0 || size > (1ll << 30))
        throw std::runtime_error("shader object has an invalid size: " + path.string());
    input.seekg(0);
    std::vector<char> bytes(static_cast<size_t>(size));
    input.read(bytes.data(), size);
    if (!input)
        throw std::runtime_error("cannot read complete shader object " + path.string());
    return bytes;
}

std::vector<Entry> read_catalog(const fs::path& path)
{
    std::istringstream input(read_text(path));
    std::vector<Entry> entries;
    std::set<std::string> keys;
    for (std::string line; std::getline(input, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        const size_t first = line.find('\t');
        const size_t second = first == std::string::npos
            ? std::string::npos
            : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos ||
            line.find('\t', second + 1) != std::string::npos)
            throw std::runtime_error("malformed shader family catalog row");
        Entry entry{
            line.substr(0, first),
            fs::u8path(line.substr(first + 1, second - first - 1)),
            fs::u8path(line.substr(second + 1))};
        if (entry.object.empty() || entry.dependencies.empty() ||
            !keys.insert(entry.key).second)
            throw std::runtime_error("invalid or duplicate shader permutation key");
        entries.push_back(std::move(entry));
    }
    if (entries.empty())
        throw std::runtime_error("shader family catalog is empty");
    if (entries.size() != 1 &&
        std::any_of(entries.begin(), entries.end(), [](const Entry& entry) {
            return entry.key.empty();
        }))
        throw std::runtime_error("a multi-permutation shader family has an empty key");
    return entries;
}

std::string trim(std::string value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    value.erase(0, first);
    value.erase(value.find_last_not_of(" \t\r\n") + 1);
    return value;
}

std::vector<std::string> parse_dependencies(
    const fs::path& depfile,
    const fs::path& workingDirectory)
{
    std::string text = read_text(depfile);
    for (size_t position = 0; (position = text.find("\\\r\n", position)) !=
         std::string::npos;)
        text.replace(position, 3, " ");
    for (size_t position = 0; (position = text.find("\\\n", position)) !=
         std::string::npos;)
        text.replace(position, 2, " ");
    const size_t delimiter = text.find(": ", 2);
    if (delimiter == std::string::npos)
        throw std::runtime_error("malformed DXC dependency file " + depfile.string());
    text = text.substr(delimiter + 2);

    std::vector<std::string> dependencies;
    std::string token;
    bool escapedSpace = false;
    for (size_t index = 0; index <= text.size(); ++index)
    {
        const char character = index == text.size() ? ' ' : text[index];
        if ((character == ' ' || character == '\t' || character == '\r' ||
             character == '\n') && !escapedSpace)
        {
            token = trim(token);
            if (!token.empty())
            {
                fs::path dependency = fs::u8path(token);
                if (dependency.is_relative())
                    dependency = workingDirectory / dependency;
                dependencies.push_back(dependency.lexically_normal().generic_string());
            }
            token.clear();
            continue;
        }
        if (character == '\\' && index + 1 < text.size() &&
            (text[index + 1] == ' ' || text[index + 1] == '\t'))
        {
            escapedSpace = true;
            continue;
        }
        token.push_back(character == '\\' ? '/' : character);
        escapedSpace = false;
    }
    return dependencies;
}

std::string escape_depfile_path(const std::string& path)
{
    std::string escaped;
    escaped.reserve(path.size());
    for (char character : path)
    {
        if (character == ' ' || character == '#')
            escaped.push_back('\\');
        else if (character == '$')
            escaped.push_back('$');
        escaped.push_back(character);
    }
    return escaped;
}

void replace_file(const fs::path& temporary, const fs::path& destination)
{
    std::error_code sizeError;
    const bool sameSize = fs::exists(destination, sizeError) && !sizeError &&
        fs::file_size(temporary, sizeError) == fs::file_size(destination, sizeError) &&
        !sizeError;
    if (sameSize)
    {
        bool equal = false;
        {
            std::ifstream candidate(temporary, std::ios::binary);
            std::ifstream current(destination, std::ios::binary);
            constexpr size_t blockSize = 64 * 1024;
            std::vector<char> candidateBlock(blockSize);
            std::vector<char> currentBlock(blockSize);
            equal = candidate && current;
            while (equal && candidate)
            {
                candidate.read(candidateBlock.data(), candidateBlock.size());
                current.read(currentBlock.data(), currentBlock.size());
                const std::streamsize count = candidate.gcount();
                equal = count == current.gcount() &&
                    std::equal(candidateBlock.begin(), candidateBlock.begin() + count,
                        currentBlock.begin());
            }
        }
        if (equal)
        {
            std::error_code removeError;
            fs::remove(temporary, removeError);
            if (removeError)
                throw std::runtime_error("cannot discard unchanged temporary file " +
                    temporary.string() + ": " + removeError.message());
            return;
        }
    }

    std::error_code error;
    fs::remove(destination, error);
    error.clear();
    fs::rename(temporary, destination, error);
    if (error)
        throw std::runtime_error("cannot publish " + destination.string() +
            ": " + error.message());
}

void write_blob(std::ostream& stream, const std::vector<Entry>& entries)
{
    if (entries.size() == 1 && entries.front().key.empty())
    {
        const std::vector<char> binary = read_binary(entries.front().object);
        stream.write(binary.data(), static_cast<std::streamsize>(binary.size()));
        return;
    }

    if (!uvsr::shader_blob::write_header(stream))
        throw std::runtime_error("cannot write shader blob header");
    for (const Entry& entry : entries)
    {
        const std::vector<char> binary = read_binary(entry.object);
        if (!uvsr::shader_blob::write_permutation(stream, entry.key,
                binary.data(), binary.size()))
            throw std::runtime_error("cannot write shader permutation");
    }
}

bool is_identifier(const std::string& value)
{
    if (value.empty() ||
        !(std::isalpha(static_cast<unsigned char>(value.front())) ||
            value.front() == '_'))
        return false;
    return std::all_of(value.begin() + 1, value.end(), [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) ||
            character == '_';
    });
}

void write_header_array(
    std::ostream& stream,
    const std::string& symbol,
    const std::vector<char>& bytes)
{
    if (!is_identifier(symbol))
        throw std::runtime_error("invalid shader header symbol");
    stream << "// Generated by uvsr_shader_blob_builder.\n"
           << "const uint8_t " << symbol << "[] = {\n";
    for (size_t index = 0; index < bytes.size(); ++index)
    {
        if (index % 20 == 0)
            stream << "    ";
        stream << static_cast<unsigned int>(
            static_cast<unsigned char>(bytes[index]));
        if (index + 1 != bytes.size())
            stream << ',';
        if (index % 20 == 19 || index + 1 == bytes.size())
            stream << '\n';
        else
            stream << ' ';
    }
    stream << "};\n";
}

void build_blob(
    const fs::path& output,
    const std::vector<Entry>& entries,
    const std::string& headerSymbol)
{
    fs::create_directories(output.parent_path());
    const fs::path temporary = temporary_path(output);
    std::error_code ignored;
    fs::remove(temporary, ignored);
    try
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            throw std::runtime_error("cannot create " + temporary.string());
        if (headerSymbol.empty())
            write_blob(stream, entries);
        else
        {
            std::ostringstream binary(std::ios::binary | std::ios::out);
            write_blob(binary, entries);
            const std::string bytes = binary.str();
            write_header_array(stream, headerSymbol,
                std::vector<char>(bytes.begin(), bytes.end()));
        }
        stream.close();
        if (!stream)
            throw std::runtime_error("cannot finish " + temporary.string());
        replace_file(temporary, output);
    }
    catch (...)
    {
        fs::remove(temporary, ignored);
        throw;
    }
}

void publish_depfile(
    const fs::path& output,
    const fs::path& depfile,
    const std::set<std::string>& dependencies)
{
    const fs::path temporary = temporary_path(depfile);
    std::error_code ignored;
    fs::remove(temporary, ignored);
    try
    {
        fs::create_directories(depfile.parent_path());
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            throw std::runtime_error("cannot create " + temporary.string());
        stream << escape_depfile_path(output.lexically_normal().generic_string()) << ':';
        for (const std::string& dependency : dependencies)
            stream << " \\\n  " << escape_depfile_path(dependency);
        stream << '\n';
        stream.close();
        if (!stream)
            throw std::runtime_error("cannot finish " + temporary.string());
        replace_file(temporary, depfile);
    }
    catch (...)
    {
        fs::remove(temporary, ignored);
        throw;
    }
}

void build_depfile(
    const fs::path& output,
    const fs::path& depfile,
    const fs::path& workingDirectory,
    const std::vector<Entry>& entries)
{
    std::set<std::string> dependencies;
    for (const Entry& entry : entries)
        for (std::string dependency : parse_dependencies(
                 entry.dependencies, workingDirectory))
            dependencies.insert(std::move(dependency));
    publish_depfile(output, depfile, dependencies);
}

void build_shader_dependency_file(
    const fs::path& source,
    const fs::path& target,
    const fs::path& depfile,
    const std::vector<fs::path>& includeDirectories)
{
    std::set<std::string> dependencies;
    scan_shader_file(
        source,
        includeDirectories,
        dependencies);
    publish_depfile(target, depfile, dependencies);
}

void run_dependency_scan(int argc, char** argv)
{
    fs::path source;
    fs::path target;
    fs::path depfile;
    std::vector<fs::path> includeDirectories;
    for (int index = 2; index < argc; ++index)
    {
        const std::string option = argv[index];
        if (index + 1 >= argc)
            throw std::runtime_error("missing value for " + option);
        const fs::path value = fs::u8path(argv[++index]);
        if (option == "--source")
            source = value;
        else if (option == "--target")
            target = value;
        else if (option == "--depfile")
            depfile = value;
        else if (option == "--include-directory")
            includeDirectories.push_back(normalized_existing_path(value));
        else
            throw std::runtime_error(
                "unknown dependency-scan option " + option);
    }
    if (source.empty() || target.empty() || depfile.empty())
        throw std::runtime_error(
            "required shader dependency-scan option is missing");
    build_shader_dependency_file(
        normalized_existing_path(source),
        target.lexically_normal(),
        depfile,
        includeDirectories);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc > 1 && std::string(argv[1]) == "--scan-dependencies")
        {
            run_dependency_scan(argc, argv);
            return 0;
        }
        fs::path output;
        fs::path depfile;
        fs::path catalog;
        fs::path workingDirectory;
        std::string headerSymbol;
        for (int index = 1; index < argc; ++index)
        {
            const std::string option = argv[index];
            if (index + 1 >= argc)
                throw std::runtime_error("missing value for " + option);
            const std::string rawValue = argv[++index];
            const fs::path value = fs::u8path(rawValue);
            if (option == "--output")
                output = value;
            else if (option == "--depfile")
                depfile = value;
            else if (option == "--catalog")
                catalog = value;
            else if (option == "--working-directory")
                workingDirectory = value;
            else if (option == "--header-symbol")
                headerSymbol = rawValue;
            else
                throw std::runtime_error("unknown option " + option);
        }
        if (output.empty() || depfile.empty() || catalog.empty() ||
            workingDirectory.empty())
            throw std::runtime_error("required shader blob builder option is missing");
        const std::vector<Entry> entries = read_catalog(catalog);
        build_blob(output, entries, headerSymbol);
        build_depfile(output, depfile, workingDirectory, entries);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "shader blob builder: " << error.what() << '\n';
        return 1;
    }
}
