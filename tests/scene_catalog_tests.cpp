#include "scene_catalog.h"
#include "json_document.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    class TemporaryDirectory
    {
    private:
        std::filesystem::path m_Path;

    public:
        TemporaryDirectory()
        {
            const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            m_Path = std::filesystem::temp_directory_path()
                / ("uvsr_scene_catalog_" + std::to_string(nonce));
            std::filesystem::create_directories(
                m_Path / "bistro_interior_retextured/components");
            std::filesystem::create_directories(
                m_Path / "bistro_interior_retextured/components/details");
            std::filesystem::create_directories(m_Path / "standalone");
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(m_Path, error);
        }

        const std::filesystem::path& GetPath() const
        {
            return m_Path;
        }
    };

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << text;
        Require(stream.good(), "failed to write scene-catalog test fixture");
    }

    std::string Generic(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    const uvsr::SceneCatalogEntry* FindByDisplayName(
        const std::vector<uvsr::SceneCatalogEntry>& catalog,
        const std::string& displayName)
    {
        const auto match = std::find_if(catalog.begin(), catalog.end(), [&displayName](const auto& entry)
        {
            return entry.DisplayName == displayName;
        });
        return match == catalog.end() ? nullptr : &*match;
    }

    std::vector<std::string> Flatten(const std::vector<uvsr::SceneCatalogEntry>& catalog)
    {
        std::vector<std::string> result;
        for (const auto& entry : catalog)
        {
            result.push_back(entry.DisplayName);
            result.push_back(entry.FileName);
        }
        return result;
    }
}

int main()
{
    try
    {
        const uvsr::json::Value json = uvsr::json::Parse(
            R"({"name":"Caf\u00e9","number":-1.25e2,"empty":null,"enabled":true})");
        Require(json.kind == uvsr::json::Value::Kind::Object &&
                json.Find("name") != nullptr &&
                json.Find("name")->string == "Caf\xc3\xa9" &&
                json.Find("number") != nullptr &&
                json.Find("number")->number == -125.0 &&
                json.Find("empty") != nullptr &&
                json.Find("empty")->kind == uvsr::json::Value::Kind::Null &&
                json.Find("enabled") != nullptr &&
                json.Find("enabled")->boolean,
            "direct JSON parser must preserve strings, numbers, null, and booleans");
        bool duplicateRejected = false;
        try
        {
            (void)uvsr::json::Parse(R"({"name":1,"name":2})");
        }
        catch (const std::runtime_error&)
        {
            duplicateRejected = true;
        }
        Require(duplicateRejected,
            "direct JSON parser must reject duplicate object properties");

        TemporaryDirectory temporary;
        const std::filesystem::path root = temporary.GetPath();
        const std::filesystem::path bistro =
            root / "bistro_interior_retextured";
        const std::filesystem::path components = bistro / "components";

        const std::filesystem::path part1 = components / "part1.glb";
        const std::filesystem::path part2 = components / "part2.glb";
        const std::filesystem::path fixtures = components / "fixtures.glb";
        const std::filesystem::path detail1 = components / "details/detail1.glb";
        const std::filesystem::path detail2 = components / "details/detail2.glb";
        const std::filesystem::path mainDescriptor =
            bistro / "main.scene.json";
        const std::filesystem::path alternateDescriptor =
            bistro / "alternate.scene.json";
        const std::filesystem::path fallbackDescriptor =
            bistro / "fallback.scene.json";
        const std::filesystem::path standalone = root / "standalone/example.glb";

        WriteText(mainDescriptor,
            R"({"displayName":"Bistro Interior","initialCamera":{"position":[1,2,3],"direction":[0,0,-2],"up":[0,3,0],"verticalFovDegrees":55},"models":["components/part1.glb","components/../components/part2.glb","components/fixtures.glb","components/details/detail1.glb","components/details/detail2.glb"],"graph":[]})");
        WriteText(alternateDescriptor,
            R"({"displayName":"Bistro Alternate","models":["components/part1.glb","components/part2.glb"],"graph":[]})");
        WriteText(fallbackDescriptor,
            R"({"displayName":"   ","initialCamera":{"position":[0,0,0],"direction":[3e38,0,0],"up":[3e38,1,0]},"models":[],"graph":[]})");

        std::vector<std::string> discovered = {
            Generic(part2),
            Generic(alternateDescriptor),
            Generic(detail2),
            Generic(standalone),
            Generic(mainDescriptor),
            Generic(fixtures),
            Generic(fallbackDescriptor),
            Generic(detail1),
            Generic(part1),
            Generic(part1), // Duplicate discovery must not duplicate a picker entry.
        };

        const auto catalog = uvsr::BuildSceneCatalog(root, discovered);

        Require(catalog.size() == 4, "catalog must hide all descriptor-owned components");
        const auto* bistroEntry = FindByDisplayName(catalog, "Bistro Interior");
        Require(bistroEntry != nullptr,
            "main descriptor must use its friendly display name");
        Require(bistroEntry->InitialCamera.has_value(),
            "valid descriptor initial camera must be retained");
        Require(
            bistroEntry->InitialCamera->Position ==
                    std::array<float, 3>{ 1.f, 2.f, 3.f } &&
                bistroEntry->InitialCamera->Direction ==
                    std::array<float, 3>{ 0.f, 0.f, -1.f } &&
                bistroEntry->InitialCamera->Up ==
                    std::array<float, 3>{ 0.f, 1.f, 0.f } &&
                bistroEntry->InitialCamera->VerticalFovDegrees == 55.f,
            "descriptor initial camera must normalize vectors and retain its pose");
        Require(FindByDisplayName(catalog, "Bistro Alternate") != nullptr,
            "alternate descriptor must remain visible when it shares components");
        const auto* fallback = FindByDisplayName(
            catalog,
            "bistro_interior_retextured/fallback.scene.json");
        Require(fallback != nullptr,
            "empty displayName must fall back to the relative descriptor path");
        Require(!fallback->InitialCamera.has_value(),
            "overflow-prone near-parallel vectors must reject an invalid initial camera");
        Require(FindByDisplayName(catalog, "standalone/example.glb") != nullptr,
            "unreferenced standalone GLB must remain visible");

        Require(uvsr::FindSceneCatalogEntry(catalog, Generic(mainDescriptor)) != nullptr,
            "exact main-descriptor lookup must succeed independently of similar names");
        Require(uvsr::FindSceneCatalogEntry(catalog, Generic(part1)) == nullptr,
            "hidden component must not resolve as a catalog entry");
        Require(uvsr::FindSceneCatalogEntry(catalog, Generic(detail1)) == nullptr,
            "nested component must not resolve as a catalog entry");

        Require(uvsr::MakeSceneDisplayName(root, standalone) == "standalone/example.glb",
            "in-tree scene display names must remain relative");
        Require(
            uvsr::MakeSceneDisplayName(
                std::filesystem::path(Generic(root) + "/"),
                mainDescriptor) ==
                "bistro_interior_retextured/main.scene.json",
            "a trailing scene-directory separator must preserve the exact "
            "relative filename");
        const std::filesystem::path similarlyPrefixedDirectory =
            root.parent_path() / (root.filename().string() + "_backup");
        const std::filesystem::path externalScene = similarlyPrefixedDirectory / "external.glb";
        Require(uvsr::MakeSceneDisplayName(root, externalScene) == Generic(externalScene),
            "a sibling path sharing the scene-directory prefix must remain external");

        std::reverse(discovered.begin(), discovered.end());
        const auto reversedCatalog = uvsr::BuildSceneCatalog(root, discovered);
        Require(Flatten(catalog) == Flatten(reversedCatalog),
            "catalog order must not depend on filesystem enumeration order");

#ifdef _WIN32
        std::string upperCasePath = Generic(mainDescriptor);
        std::transform(upperCasePath.begin(), upperCasePath.end(), upperCasePath.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::toupper(c));
        });
        Require(uvsr::FindSceneCatalogEntry(catalog, upperCasePath) != nullptr,
            "Windows catalog lookup must follow native case-insensitive path semantics");
#endif

        std::cout << "scene catalog reference tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "scene catalog reference tests failed: " << error.what() << '\n';
        return 1;
    }
}
