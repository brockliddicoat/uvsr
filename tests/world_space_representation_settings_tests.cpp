#include "world_space_representation_settings.h"
#include "world_space_representation_contract.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <cstddef>

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
        enum class MaterialDomain
        {
            Opaque,
            AlphaTested,
            AlphaBlended,
            Transmissive,
            Count
        };

        assert(IsRayVisibilityMaterialDomainSupported(
            MaterialDomain::Opaque,
            MaterialDomain::Opaque,
            MaterialDomain::AlphaTested));
        assert(IsRayVisibilityMaterialDomainSupported(
            MaterialDomain::AlphaTested,
            MaterialDomain::Opaque,
            MaterialDomain::AlphaTested));
        assert(!IsRayVisibilityMaterialDomainSupported(
            MaterialDomain::AlphaBlended,
            MaterialDomain::Opaque,
            MaterialDomain::AlphaTested));
        assert(!IsRayVisibilityMaterialDomainSupported(
            MaterialDomain::Transmissive,
            MaterialDomain::Opaque,
            MaterialDomain::AlphaTested));
        assert(IsRayVisibilityMaterialDomainOpaque(
            MaterialDomain::Opaque,
            MaterialDomain::Opaque));
        assert(!IsRayVisibilityMaterialDomainOpaque(
            MaterialDomain::AlphaTested,
            MaterialDomain::Opaque));

        std::uint32_t offset = 0u;
        assert(TryResolveRayVisibilityGeometryMapOffset(0u, offset));
        assert(offset == 0u);
        assert(TryResolveRayVisibilityGeometryMapOffset(
            MaximumRayVisibilityGeometryMapOffset,
            offset));
        assert(offset == MaximumRayVisibilityGeometryMapOffset);
        assert(!TryResolveRayVisibilityGeometryMapOffset(
            std::uint64_t(MaximumRayVisibilityGeometryMapOffset) + 1u,
            offset));

        assert(RetainsRayVisibilityGeometryMap(
            WorldSpaceRepresentationInvalidation::None));
        assert(RetainsRayVisibilityGeometryMap(
            WorldSpaceRepresentationInvalidation::Tlas));
        assert(!RetainsRayVisibilityGeometryMap(
            WorldSpaceRepresentationInvalidation::BlasAndTlas));
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
