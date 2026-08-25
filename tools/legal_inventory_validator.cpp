#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view RequiredNotice =
        "Required Notice: UVSR | https://github.com/brockliddicoat/uvsr";
    constexpr std::string_view PolyformBodySha256 =
        "c0ea4a896d2c8c394b29f9427589996db826cd501c512279ff0ed3ef48fabbe5";

    [[nodiscard]] std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot read " + path.u8string());
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
    }

    [[nodiscard]] std::string NormalizeLineEndings(std::string_view input)
    {
        std::string output;
        output.reserve(input.size());
        for (std::size_t index = 0u; index < input.size(); ++index)
        {
            if (input[index] == '\r' && index + 1u < input.size() &&
                input[index + 1u] == '\n')
            {
                continue;
            }
            output.push_back(input[index]);
        }
        return output;
    }

    [[nodiscard]] std::string Sha256(std::string_view input)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD objectSize = 0u;
        DWORD resultSize = 0u;
        std::vector<unsigned char> object;
        std::array<unsigned char, 32> digest{};
        const auto cleanup = [&]
        {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0u);
        };

        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0u);
        if (status >= 0)
        {
            status = BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectSize),
                sizeof(objectSize),
                &resultSize,
                0u);
        }
        if (status >= 0)
        {
            object.resize(objectSize);
            status = BCryptCreateHash(
                algorithm,
                &hash,
                object.data(),
                static_cast<ULONG>(object.size()),
                nullptr,
                0u,
                0u);
        }
        if (status >= 0)
        {
            status = BCryptHashData(
                hash,
                reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                static_cast<ULONG>(input.size()),
                0u);
        }
        if (status >= 0)
        {
            status = BCryptFinishHash(
                hash,
                digest.data(),
                static_cast<ULONG>(digest.size()),
                0u);
        }
        cleanup();
        if (status < 0)
            throw std::runtime_error("Windows SHA-256 operation failed");

        constexpr char Hex[] = "0123456789abcdef";
        std::string text;
        text.reserve(digest.size() * 2u);
        for (const unsigned char byte : digest)
        {
            text.push_back(Hex[byte >> 4u]);
            text.push_back(Hex[byte & 0x0fu]);
        }
        return text;
    }

    void CheckLicense(const std::filesystem::path& root)
    {
        const std::string license = NormalizeLineEndings(
            ReadFile(root / "LICENSE.md"));
        const std::size_t separator = license.find("\n\n");
        if (separator == std::string::npos ||
            std::string_view(license).substr(0u, separator) != RequiredNotice)
        {
            throw std::runtime_error(
                "LICENSE.md has an unexpected required notice");
        }
        if (Sha256(std::string_view(license).substr(separator + 2u)) !=
            PolyformBodySha256)
        {
            throw std::runtime_error(
                "Polyform Noncommercial 1.0.0 body was modified");
        }
    }

    void CheckLayout(const std::filesystem::path& root)
    {
        for (const std::filesystem::path& legacy : {
                root / "src" / "third_party",
                root / "legal" / "sources",
                root / "legal" / "code-samples" })
        {
            if (std::filesystem::exists(legacy))
                throw std::runtime_error("legacy path remains: " + legacy.u8string());
        }
        const std::filesystem::path thirdParty = root / "third_party";
        const std::set<std::string> directPins = { "imgui", "nvrhi" };
        if (!std::filesystem::is_directory(thirdParty))
            throw std::runtime_error("direct third-party pins are missing");
        std::set<std::string> foundPins;
        for (const auto& entry : std::filesystem::directory_iterator(thirdParty))
        {
            const std::string name = entry.path().filename().u8string();
            if (!entry.is_directory() || directPins.count(name) == 0u)
            {
                throw std::runtime_error(
                    "unexpected root third_party entry: " + name);
            }
            foundPins.insert(name);
        }
        if (foundPins != directPins)
            throw std::runtime_error("required direct third-party pin is missing");
        for (const std::filesystem::path& required : {
                root / "legal" / "README.md",
                root / "legal" / "licenses" / "README.md",
                root / "legal" / "documentation" / "README.md",
                root / "legal" / "documentation" / "commercial-licensing.md",
                root / "legal" / "documentation" /
                    "contributor-agreement-privacy-notice.md",
                root / "legal" / "documentation" / "third-party-notices.md" })
        {
            if (!std::filesystem::is_regular_file(required))
                throw std::runtime_error("required legal entry is missing");
        }
    }

    void CheckDocumentationIndex(const std::filesystem::path& root)
    {
        const std::filesystem::path documentation =
            root / "legal" / "documentation";
        const std::string index = ReadFile(documentation / "README.md");
        const std::set<std::string> nonRecords = {
            "commercial-licensing.md",
            "contributor-agreement-privacy-notice.md",
            "third-party-notices.md"
        };
        std::set<std::string> records;
        for (const auto& entry : std::filesystem::directory_iterator(documentation))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".md")
                continue;
            const std::string name = entry.path().filename().u8string();
            if (name != "README.md" && nonRecords.count(name) == 0u)
                records.insert(name);
        }

        const std::regex linkPattern(R"(\[[^\]]+\]\(([^\)]+)\))");
        std::set<std::string> linked;
        for (std::sregex_iterator iterator(
                 index.begin(), index.end(), linkPattern), end;
             iterator != end;
             ++iterator)
        {
            std::string target = (*iterator)[1].str();
            const std::size_t fragment = target.find('#');
            if (fragment != std::string::npos)
                target.erase(fragment);
            if (target.empty() || target.find("://") != std::string::npos ||
                target.rfind("mailto:", 0u) == 0u)
            {
                continue;
            }
            const std::filesystem::path resolved =
                (documentation / target).lexically_normal();
            if (resolved.parent_path() == documentation &&
                resolved.extension() == ".md")
            {
                if (!std::filesystem::is_regular_file(resolved))
                    throw std::runtime_error("documentation index has a broken link");
                linked.insert(resolved.filename().u8string());
            }
        }
        for (const std::string& record : records)
        {
            if (linked.count(record) == 0u)
            {
                throw std::runtime_error(
                    "documentation index omits " + record);
            }
        }
    }

    [[nodiscard]] std::set<std::string> ParseRequiredMetadataFields(
        std::string_view json)
    {
        std::set<std::string> fields;
        std::size_t index = json.find('{');
        if (index == std::string_view::npos)
            throw std::runtime_error("CLA metadata is not an object");
        int depth = 1;
        bool quoted = false;
        bool escaped = false;
        for (++index; index < json.size() && depth > 0; ++index)
        {
            const char character = json[index];
            if (quoted)
            {
                if (escaped)
                    escaped = false;
                else if (character == '\\')
                    escaped = true;
                else if (character == '"')
                    quoted = false;
                continue;
            }
            if (character == '"' && depth == 1)
            {
                const std::size_t keyEnd = json.find('"', index + 1u);
                if (keyEnd == std::string_view::npos)
                    throw std::runtime_error("CLA metadata has an unterminated key");
                const std::string key(json.substr(index + 1u, keyEnd - index - 1u));
                std::size_t objectBegin = json.find(':', keyEnd + 1u);
                if (objectBegin == std::string_view::npos)
                    throw std::runtime_error("CLA metadata key has no value");
                objectBegin = json.find_first_not_of(" \t\r\n", objectBegin + 1u);
                if (objectBegin == std::string_view::npos || json[objectBegin] != '{')
                    throw std::runtime_error("CLA metadata field is not an object");
                int objectDepth = 1;
                std::size_t objectEnd = objectBegin + 1u;
                bool objectQuoted = false;
                bool objectEscaped = false;
                for (; objectEnd < json.size() && objectDepth > 0; ++objectEnd)
                {
                    const char valueCharacter = json[objectEnd];
                    if (objectQuoted)
                    {
                        if (objectEscaped)
                            objectEscaped = false;
                        else if (valueCharacter == '\\')
                            objectEscaped = true;
                        else if (valueCharacter == '"')
                            objectQuoted = false;
                    }
                    else if (valueCharacter == '"')
                        objectQuoted = true;
                    else if (valueCharacter == '{')
                        ++objectDepth;
                    else if (valueCharacter == '}')
                        --objectDepth;
                }
                if (objectDepth != 0 ||
                    json.substr(objectBegin, objectEnd - objectBegin).find(
                        R"("required": true)") == std::string_view::npos)
                {
                    throw std::runtime_error(
                        "CLA metadata field is not explicitly required");
                }
                fields.insert(key);
                index = objectEnd - 1u;
                continue;
            }
            if (character == '"')
                quoted = true;
            else if (character == '{')
                ++depth;
            else if (character == '}')
                --depth;
        }
        return fields;
    }

    void CheckMetadata(const std::filesystem::path& root)
    {
        const std::set<std::string> fields = ParseRequiredMetadataFields(
            ReadFile(root / "legal" / "documentation" /
                "cla-assistant-metadata.json"));
        if (fields != std::set<std::string>{
                "agreement", "email", "name", "ownership" })
        {
            throw std::runtime_error("CLA metadata fields differ from the schema");
        }
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2)
    {
        std::cerr << "usage: uvsr-legal-inventory <repository-root>\n";
        return EXIT_FAILURE;
    }
    try
    {
        const std::filesystem::path root =
            std::filesystem::weakly_canonical(arguments[1]);
        CheckLicense(root);
        CheckLayout(root);
        CheckDocumentationIndex(root);
        CheckMetadata(root);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Legal inventory check failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Legal inventory check passed.\n";
    return EXIT_SUCCESS;
}
