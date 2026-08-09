#include "world_space_representation_settings.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
    using namespace uvsr;

    constexpr std::array<BvhBuildPreference, 3> BvhPreferences = {
        BvhBuildPreference::FastTrace,
        BvhBuildPreference::Balanced,
        BvhBuildPreference::FastBuild
    };
    constexpr std::array<BlasUpdateMode, 2> BlasModes = {
        BlasUpdateMode::Rebuild,
        BlasUpdateMode::Refit
    };
    constexpr std::array<TlasUpdateMode, 2> TlasModes = {
        TlasUpdateMode::Rebuild,
        TlasUpdateMode::Refit
    };

    std::string ReadSource(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        assert(stream.good());
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
    }

    std::string Canonicalize(std::string_view source)
    {
        std::string canonical;
        canonical.reserve(source.size());
        for (const char character : source)
        {
            const unsigned char byte =
                static_cast<unsigned char>(character);
            if (!std::isspace(byte))
            {
                canonical.push_back(
                    static_cast<char>(std::tolower(byte)));
            }
        }
        return canonical;
    }

    std::string_view ExtractSection(
        std::string_view source,
        std::string_view beginToken,
        std::string_view endToken)
    {
        const std::size_t begin = source.find(beginToken);
        assert(begin != std::string_view::npos);
        const std::size_t end = source.find(
            endToken,
            begin + beginToken.size());
        assert(end != std::string_view::npos);
        return source.substr(begin, end - begin);
    }

    constexpr WorldSpaceRepresentationInvalidation ExpectedInvalidation(
        const WorldSpaceRepresentationSettings& previous,
        const WorldSpaceRepresentationSettings& next)
    {
        if (previous.bvhBuildPreference != next.bvhBuildPreference ||
            previous.blasUpdateMode != next.blasUpdateMode)
        {
            return WorldSpaceRepresentationInvalidation::BlasAndTlas;
        }
        if (previous.tlasUpdateMode != next.tlasUpdateMode)
            return WorldSpaceRepresentationInvalidation::Tlas;
        return WorldSpaceRepresentationInvalidation::None;
    }

    constexpr auto MakeSettingsMatrix()
    {
        std::array<WorldSpaceRepresentationSettings, 12> settings{};
        std::size_t index = 0u;
        for (const BvhBuildPreference bvhPreference : BvhPreferences)
        {
            for (const BlasUpdateMode blasMode : BlasModes)
            {
                for (const TlasUpdateMode tlasMode : TlasModes)
                {
                    settings[index++] = {
                        bvhPreference,
                        blasMode,
                        tlasMode
                    };
                }
            }
        }
        return settings;
    }

    void TestDefaultSettings()
    {
        const WorldSpaceRepresentationSettings settings;
        assert(settings.bvhBuildPreference ==
            BvhBuildPreference::FastTrace);
        assert(settings.blasUpdateMode == BlasUpdateMode::Refit);
        assert(settings.tlasUpdateMode == TlasUpdateMode::Refit);
        assert(settings.allowRayTraversal);
        assert(GetWorldSpaceRepresentationInvalidation(
            settings,
            settings) == WorldSpaceRepresentationInvalidation::None);
    }

    void TestRayTraversalPolicyDoesNotInvalidateRepresentation()
    {
        const WorldSpaceRepresentationSettings defaults;
        WorldSpaceRepresentationSettings traversalDisabled = defaults;
        traversalDisabled.allowRayTraversal = false;

        assert(GetWorldSpaceRepresentationInvalidation(
            defaults,
            traversalDisabled) == WorldSpaceRepresentationInvalidation::None);
        assert(GetWorldSpaceRepresentationInvalidation(
            traversalDisabled,
            defaults) == WorldSpaceRepresentationInvalidation::None);

        traversalDisabled.tlasUpdateMode = TlasUpdateMode::Rebuild;
        assert(GetWorldSpaceRepresentationInvalidation(
            defaults,
            traversalDisabled) == WorldSpaceRepresentationInvalidation::Tlas);
        traversalDisabled.blasUpdateMode = BlasUpdateMode::Rebuild;
        assert(GetWorldSpaceRepresentationInvalidation(
            defaults,
            traversalDisabled) ==
            WorldSpaceRepresentationInvalidation::BlasAndTlas);
    }

    void TestCompleteInvalidationMatrix()
    {
        constexpr auto SettingsMatrix = MakeSettingsMatrix();
        static_assert(SettingsMatrix.size() == 12u);

        std::size_t noneCount = 0u;
        std::size_t tlasCount = 0u;
        std::size_t blasAndTlasCount = 0u;
        for (const WorldSpaceRepresentationSettings& previous :
            SettingsMatrix)
        {
            for (const WorldSpaceRepresentationSettings& next :
                SettingsMatrix)
            {
                const WorldSpaceRepresentationInvalidation expected =
                    ExpectedInvalidation(previous, next);
                const WorldSpaceRepresentationInvalidation actual =
                    GetWorldSpaceRepresentationInvalidation(
                        previous,
                        next);
                assert(actual == expected);

                switch (actual)
                {
                case WorldSpaceRepresentationInvalidation::None:
                    ++noneCount;
                    break;
                case WorldSpaceRepresentationInvalidation::Tlas:
                    ++tlasCount;
                    break;
                case WorldSpaceRepresentationInvalidation::BlasAndTlas:
                    ++blasAndTlasCount;
                    break;
                }
            }
        }

        assert(noneCount == 12u);
        assert(tlasCount == 12u);
        assert(blasAndTlasCount == 120u);
    }

    void TestInvalidationPrecedence()
    {
        const WorldSpaceRepresentationSettings defaults;

        WorldSpaceRepresentationSettings tlasOnly = defaults;
        tlasOnly.tlasUpdateMode = TlasUpdateMode::Rebuild;
        assert(GetWorldSpaceRepresentationInvalidation(
            defaults,
            tlasOnly) == WorldSpaceRepresentationInvalidation::Tlas);

        WorldSpaceRepresentationSettings blasChange = defaults;
        blasChange.blasUpdateMode = BlasUpdateMode::Rebuild;
        blasChange.tlasUpdateMode = TlasUpdateMode::Rebuild;
        assert(GetWorldSpaceRepresentationInvalidation(
            defaults,
            blasChange) ==
            WorldSpaceRepresentationInvalidation::BlasAndTlas);

        WorldSpaceRepresentationSettings bvhChange = defaults;
        bvhChange.bvhBuildPreference = BvhBuildPreference::Balanced;
        bvhChange.tlasUpdateMode = TlasUpdateMode::Rebuild;
        assert(GetWorldSpaceRepresentationInvalidation(
            defaults,
            bvhChange) ==
            WorldSpaceRepresentationInvalidation::BlasAndTlas);

        bvhChange.bvhBuildPreference = BvhBuildPreference::FastBuild;
        assert(GetWorldSpaceRepresentationInvalidation(
            defaults,
            bvhChange) ==
            WorldSpaceRepresentationInvalidation::BlasAndTlas);
    }

    void TestMaterialVisibilityTopologyContract()
    {
        const std::filesystem::path sourceRoot =
            std::filesystem::path(__FILE__).parent_path().parent_path();
        const std::string header = Canonicalize(ReadSource(
            sourceRoot / "src/world_space_representation.h"));
        const std::string source = Canonicalize(ReadSource(
            sourceRoot / "src/world_space_representation.cpp"));

        assert(header.find(
            "maximumrayvisibilitygeometrymapoffset=0x00ffffffu;") !=
            std::string::npos);
        assert(header.find(
            "returnoffset<=maximumrayvisibilitygeometrymapoffset;") !=
            std::string::npos);

        const std::string_view meshDescription = ExtractSection(
            source,
            "boolbuildmeshdescription(",
            "booltransformsequal(");
        const std::size_t domainFilter = meshDescription.find(
            "if(domain!=materialdomain::opaque&&"
                "domain!=materialdomain::alphatested){continue;}");
        const std::size_t appendGeometry = meshDescription.find(
            "description.addbottomlevelgeometry(");
        const std::size_t appendMapIndex = meshDescription.find(
            "geometryindices.push_back("
                "uint32_t(geometry->globalgeometryindex));");
        assert(domainFilter != std::string_view::npos);
        assert(appendGeometry != std::string_view::npos);
        assert(appendMapIndex != std::string_view::npos);
        assert(domainFilter < appendGeometry);
        assert(appendGeometry < appendMapIndex);
        assert(meshDescription.find("alphablended") ==
            std::string_view::npos);
        assert(meshDescription.find("transmissive") ==
            std::string_view::npos);

        const std::string_view beginGeneration = ExtractSection(
            source,
            "boolworldspacerepresentation::begingeneration(",
            "boolworldspacerepresentation::buildnextblas(");
        const std::size_t clearUpload = beginGeneration.find(
            "m_geometryindexmapupload.clear();");
        const std::size_t releaseMap = beginGeneration.find(
            "m_geometryindexmap=nullptr;");
        const std::size_t clearUploaded = beginGeneration.find(
            "m_geometryindexmapuploaded=false;");
        const std::size_t offsetGuard = beginGeneration.find(
            "if(!israyvisibilitygeometrymapoffsetsupported("
                "m_geometryindexmapupload.size()))");
        const std::size_t assignOffset = beginGeneration.find(
            "record.geometrymapoffset=uint32_t("
                "m_geometryindexmapupload.size());");
        const std::size_t appendIndices = beginGeneration.find(
            "m_geometryindexmapupload.insert("
                "m_geometryindexmapupload.end(),"
                "geometryindices.begin(),geometryindices.end());");
        assert(clearUpload != std::string_view::npos);
        assert(releaseMap != std::string_view::npos);
        assert(clearUploaded != std::string_view::npos);
        assert(offsetGuard != std::string_view::npos);
        assert(assignOffset != std::string_view::npos);
        assert(appendIndices != std::string_view::npos);
        assert(clearUpload < offsetGuard);
        assert(offsetGuard < assignOffset);
        assert(assignOffset < appendIndices);

        const std::string_view tlasInvalidation = ExtractSection(
            source,
            "voidworldspacerepresentation::invalidate(",
            "voidworldspacerepresentation::fail(");
        assert(tlasInvalidation.find("m_geometryindexmap") ==
            std::string_view::npos);
        assert(source.find(
            ".setinstancecontributiontohitgroupindex("
                "found->second->geometrymapoffset)") !=
            std::string::npos);
        assert(source.find(
            "commandlist->writebuffer(m_geometryindexmap,"
                "m_geometryindexmapupload.data(),") !=
            std::string::npos);
    }
}

int main()
{
    TestDefaultSettings();
    TestRayTraversalPolicyDoesNotInvalidateRepresentation();
    TestCompleteInvalidationMatrix();
    TestInvalidationPrecedence();
    TestMaterialVisibilityTopologyContract();
    return 0;
}
