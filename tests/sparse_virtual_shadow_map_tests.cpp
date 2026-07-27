#include "sparse_virtual_shadow_map.h"
#include "svsm_motion_benchmark.h"
#include "taa_miniengine_reference.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using namespace uvsr;

#undef assert
#define assert(...) \
    do { \
        if (!(static_cast<bool>(__VA_ARGS__))) \
        { \
            std::fprintf( \
                stderr, \
                "%s:%d: assertion failed: %s\n", \
                __FILE__, \
                __LINE__, \
                #__VA_ARGS__); \
            std::abort(); \
        } \
    } while (false)

namespace
{
    struct ReceiverTransformToken
    {
        uint32_t value = 0u;
    };

    ReceiverTransformToken operator*(
        ReceiverTransformToken left,
        ReceiverTransformToken right)
    {
        return { left.value * 10u + right.value };
    }

    struct ObjectInvalidationResolverTestContext
    {
        std::array<SvsmObjectInvalidationMode, 4u> modes = {
            SvsmObjectInvalidationMode::Auto,
            SvsmObjectInvalidationMode::Always,
            SvsmObjectInvalidationMode::Rigid,
            SvsmObjectInvalidationMode::Static
        };
        const void* primaryInstanceIdentity = nullptr;
        const void* secondaryInstanceIdentity = nullptr;
        bool fail = false;
        bool returnInvalidMode = false;
        bool preserveInputMode = false;
    };

    bool ResolveObjectInvalidationModeForTest(
        const void* opaqueContext,
        const SvsmObjectInvalidationResolverKey& key,
        SvsmObjectInvalidationMode& mode)
    {
        const auto* context =
            static_cast<const ObjectInvalidationResolverTestContext*>(
                opaqueContext);
        if (!context ||
            context->fail ||
            key.instanceIdentity == nullptr ||
            key.geometryIdentity == nullptr ||
            key.geometryOrdinal >= context->modes.size())
        {
            return false;
        }
        if (context->preserveInputMode)
        {
            return key.instanceIdentity ==
                    context->primaryInstanceIdentity ||
                key.instanceIdentity ==
                    context->secondaryInstanceIdentity;
        }
        mode = context->returnInvalidMode
            ? static_cast<SvsmObjectInvalidationMode>(
                std::numeric_limits<uint32_t>::max())
            : (key.instanceIdentity ==
                    context->secondaryInstanceIdentity
                ? SvsmObjectInvalidationMode::Static
                : context->modes[key.geometryOrdinal]);
        return key.instanceIdentity ==
                context->primaryInstanceIdentity ||
            key.instanceIdentity ==
                context->secondaryInstanceIdentity;
    }

    void TestPerObjectInvalidationResolver()
    {
        ObjectInvalidationResolverTestContext context;
        const uint32_t configurationIdentityA = 1u;
        const uint32_t configurationIdentityB = 2u;
        const uint32_t geometryIdentity = 3u;
        const uint32_t instanceIdentityA = 4u;
        const uint32_t instanceIdentityB = 5u;
        context.primaryInstanceIdentity = &instanceIdentityA;
        context.secondaryInstanceIdentity = &instanceIdentityB;
        SvsmObjectInvalidationResolver resolver;
        resolver.configurationIdentity = &configurationIdentityA;
        resolver.revision = 7u;
        resolver.context = &context;
        resolver.resolve =
            ResolveObjectInvalidationModeForTest;
        assert(resolver.IsValid());

        for (const SvsmObjectInvalidationMode defaultMode : {
            SvsmObjectInvalidationMode::Auto,
            SvsmObjectInvalidationMode::Always,
            SvsmObjectInvalidationMode::Rigid,
            SvsmObjectInvalidationMode::Static })
        {
            SvsmObjectInvalidationMode fallback =
                static_cast<SvsmObjectInvalidationMode>(
                    std::numeric_limits<uint32_t>::max());
            assert(TryResolveSvsmObjectInvalidationMode(
                defaultMode,
                nullptr,
                { 41u, 0u, &geometryIdentity,
                    &instanceIdentityA },
                fallback));
            assert(fallback == defaultMode);
        }

        for (uint32_t ordinal = 0u;
            ordinal < context.modes.size();
            ++ordinal)
        {
            SvsmObjectInvalidationMode mode =
                SvsmObjectInvalidationMode::Auto;
            assert(TryResolveSvsmObjectInvalidationMode(
                SvsmObjectInvalidationMode::Static,
                &resolver,
                { 41u, ordinal, &geometryIdentity,
                    &instanceIdentityA },
                mode));
            assert(mode == context.modes[ordinal]);
        }

        // Structural edits may renumber the same live MeshInstance. Its
        // resolver result remains keyed by stable identity, while removal and
        // re-addition receives a different identity even when geometry is
        // shared.
        SvsmObjectInvalidationMode renumberedMode =
            SvsmObjectInvalidationMode::Static;
        assert(resolver.TryResolve(
            { 3u, 2u, &geometryIdentity, &instanceIdentityA },
            renumberedMode));
        assert(renumberedMode ==
            SvsmObjectInvalidationMode::Rigid);
        SvsmObjectInvalidationMode readdedSharedGeometryMode =
            SvsmObjectInvalidationMode::Auto;
        assert(resolver.TryResolve(
            { 41u, 0u, &geometryIdentity, &instanceIdentityB },
            readdedSharedGeometryMode));
        assert(readdedSharedGeometryMode ==
            SvsmObjectInvalidationMode::Static);

        SvsmObjectInvalidationResolver same = resolver;
        assert(resolver.HasSameConfiguration(same));
        assert(HasSameSvsmObjectInvalidationPolicyConfiguration(
            SvsmObjectInvalidationMode::Auto,
            &resolver,
            SvsmObjectInvalidationMode::Auto,
            &same));
        assert(!HasSameSvsmObjectInvalidationPolicyConfiguration(
            SvsmObjectInvalidationMode::Auto,
            &resolver,
            SvsmObjectInvalidationMode::Rigid,
            &same));
        context.preserveInputMode = true;
        SvsmObjectInvalidationMode retainedDefault =
            SvsmObjectInvalidationMode::Always;
        assert(TryResolveSvsmObjectInvalidationMode(
            SvsmObjectInvalidationMode::Auto,
            &resolver,
            { 41u, 0u, &geometryIdentity, &instanceIdentityA },
            retainedDefault));
        assert(retainedDefault ==
            SvsmObjectInvalidationMode::Auto);
        assert(TryResolveSvsmObjectInvalidationMode(
            SvsmObjectInvalidationMode::Rigid,
            &resolver,
            { 41u, 0u, &geometryIdentity, &instanceIdentityA },
            retainedDefault));
        assert(retainedDefault ==
            SvsmObjectInvalidationMode::Rigid);
        context.preserveInputMode = false;
        same.revision = resolver.revision + 1u;
        assert(!resolver.HasSameConfiguration(same));
        same = resolver;
        same.configurationIdentity = &configurationIdentityB;
        assert(!resolver.HasSameConfiguration(same));
        same = resolver;
        same.context = nullptr;
        assert(!resolver.HasSameConfiguration(same));

        SvsmObjectInvalidationResolver invalidIdentity = resolver;
        invalidIdentity.configurationIdentity = nullptr;
        assert(!invalidIdentity.IsValid());
        SvsmObjectInvalidationMode mode =
            SvsmObjectInvalidationMode::Auto;
        assert(!invalidIdentity.TryResolve(
            { 41u, 0u, &geometryIdentity, &instanceIdentityA },
            mode));
        mode = static_cast<SvsmObjectInvalidationMode>(
            std::numeric_limits<uint32_t>::max());
        assert(!TryResolveSvsmObjectInvalidationMode(
            SvsmObjectInvalidationMode::Rigid,
            &invalidIdentity,
            { 41u, 0u, &geometryIdentity, &instanceIdentityA },
            mode));
        assert(mode == SvsmObjectInvalidationMode::Rigid);

        context.fail = true;
        assert(!resolver.TryResolve(
            { 41u, 0u, &geometryIdentity, &instanceIdentityA },
            mode));
        context.fail = false;
        context.returnInvalidMode = true;
        assert(!resolver.TryResolve(
            { 41u, 0u, &geometryIdentity, &instanceIdentityA },
            mode));
        context.returnInvalidMode = false;

        assert(IsSvsmObjectInvalidationModeValid(
            SvsmObjectInvalidationMode::Auto));
        assert(IsSvsmObjectInvalidationModeValid(
            SvsmObjectInvalidationMode::Always));
        assert(IsSvsmObjectInvalidationModeValid(
            SvsmObjectInvalidationMode::Rigid));
        assert(IsSvsmObjectInvalidationModeValid(
            SvsmObjectInvalidationMode::Static));
        assert(!IsSvsmObjectInvalidationModeValid(
            static_cast<SvsmObjectInvalidationMode>(
                std::numeric_limits<uint32_t>::max())));
    }

    void TestPagePacking()
    {
        assert(SvsmSparseFlagAllocationBudgetSaturationEarlyOut ==
            8192u);
        assert(SvsmSparseFlagScatterAlphaTestEarlyReject == 4096u);
        assert(SvsmSparseFlagDirtyPageScatterAmplificationGuard ==
            16384u);
        assert(SvsmSparseFlagCoarsestPageRenderBudget == 32768u);
        assert(SvsmSparseFlagBilinearPcf == 65536u);
        assert(SvsmSparseFlagPairedStaticDynamicDepth == 131072u);
        assert(SvsmSparseFlagPreservePhysicalMappings == 262144u);
        assert(SvsmSparseFlagStaticDepthHierarchyCulling == 524288u);
        assert(SvsmSparseFlagStaticDepthHierarchyResource == 1048576u);
        assert(SvsmSparseFlagStaticDepthHierarchyBootstrap == 2097152u);
        assert((SvsmPageStaticDirtyBit &
            (SvsmPageAgeMask << 18u)) == 0u);
        assert((SvsmPacketStaticCasterBit &
            SvsmPacketObjectInstanceMask) == 0u);
        assert((SvsmPacketPageRuntimePerPageBit &
            SvsmPacketPageRuntimeFailOpenBit) == 0u);
        assert((SvsmPacketPageRuntimeCountMask &
            (SvsmPacketPageRuntimePerPageBit |
                SvsmPacketPageRuntimeFailOpenBit)) == 0u);

        SvsmPageMetadata input;
        input.physicalPage = 32767u;
        input.age = SvsmPageAgeMask;
        input.resident = true;
        input.required = true;
        input.dirty = false;
        input.staticDirty = true;

        const uint32_t packed = PackSvsmPageMetadata(input);
        const SvsmPageMetadata output = UnpackSvsmPageMetadata(packed);
        assert(output.physicalPage == input.physicalPage);
        assert(output.age == input.age);
        assert(output.resident);
        assert(output.required);
        assert(!output.dirty);
        assert(output.staticDirty);

        input.resident = false;
        input.dirty = true;
        const SvsmPageMetadata nonresident =
            UnpackSvsmPageMetadata(PackSvsmPageMetadata(input));
        assert(nonresident.physicalPage == SvsmInvalidPhysicalPage);
        assert(!nonresident.resident);
        assert(nonresident.required);
        assert(nonresident.dirty);
        assert(nonresident.staticDirty);

        const uint32_t maximumOwner =
            SvsmClipmapCount * SvsmPagesPerClipmap - 1u;
        const uint32_t compact = PackSvsmCompactRenderPage(
            maximumOwner, SvsmPagesPerClipmap - 1u);
        assert(UnpackSvsmCompactRenderPageOwner(compact) ==
            maximumOwner);
        assert(UnpackSvsmCompactRenderPagePhysical(compact) ==
            SvsmPagesPerClipmap - 1u);
    }

    void TestPairedStaticDynamicDepth()
    {
        assert(GetSvsmPairedDepthPageAction(
            false, true, true, false) ==
            SvsmPairedDepthPageAction::ClearMerged);
        assert(GetSvsmPairedDepthPageAction(
            true, true, true, true) ==
            SvsmPairedDepthPageAction::ClearBoth);
        assert(GetSvsmPairedDepthPageAction(
            true, true, true, false) ==
            SvsmPairedDepthPageAction::RestoreStaticToMerged);
        assert(GetSvsmPairedDepthPageAction(
            true, false, true, true) ==
            SvsmPairedDepthPageAction::None);
        assert(GetSvsmPairedDepthPageAction(
            true, true, false, true) ==
            SvsmPairedDepthPageAction::None);

        constexpr uint32_t empty = 0u;
        constexpr uint32_t staticDepth = 0x3f000000u;
        constexpr uint32_t fartherDynamicDepth = 0x3e800000u;
        constexpr uint32_t nearerDynamicDepth = 0x3f400000u;
        static_assert(MergeSvsmReverseDepth(empty, staticDepth) ==
            staticDepth);
        static_assert(MergeSvsmReverseDepth(
            staticDepth, fartherDynamicDepth) == staticDepth);
        static_assert(MergeSvsmReverseDepth(
            staticDepth, nearerDynamicDepth) == nearerDynamicDepth);

        const uint32_t localStatic =
            PackSvsmLocalInvalidationPage(24575u, true);
        assert(UnpackSvsmLocalInvalidationOwner(localStatic) == 24575u);
        assert(IsSvsmLocalInvalidationStatic(localStatic));
        const uint32_t localDynamic =
            PackSvsmLocalInvalidationPage(17u, false);
        assert(UnpackSvsmLocalInvalidationOwner(localDynamic) == 17u);
        assert(!IsSvsmLocalInvalidationStatic(localDynamic));
        assert(ShouldInvalidateSvsmStaticDepth(true, false));
        assert(ShouldInvalidateSvsmStaticDepth(false, true));
        assert(!ShouldInvalidateSvsmStaticDepth(false, false));
        const SvsmCasterInvalidationClasses staticToDynamic =
            GetSvsmCasterInvalidationClasses(true, false);
        assert(staticToDynamic.previousStatic);
        assert(!staticToDynamic.currentStatic);
        const SvsmCasterInvalidationClasses dynamicToStatic =
            GetSvsmCasterInvalidationClasses(false, true);
        assert(!dynamicToStatic.previousStatic);
        assert(dynamicToStatic.currentStatic);

        assert(GetEffectiveSvsmFullInvalidation(false, false) == false);
        assert(GetEffectiveSvsmFullInvalidation(true, false));
        assert(GetEffectiveSvsmFullInvalidation(false, true));
        assert(IsSvsmCasterSnapshotTrackable(
            false, true, false));
        assert(!IsSvsmCasterSnapshotTrackable(
            false, false, true));
        assert(IsSvsmCasterSnapshotTrackable(
            true, false, true));
        assert(!IsSvsmCasterSnapshotTrackable(
            true, true, false));
        assert(CanRetainSvsmUnboundedDeformingCaster(
            true, true, true, true,
            false, false, false, false));
        assert(!CanRetainSvsmUnboundedDeformingCaster(
            true, true, true, true,
            true, false, false, false));
        assert(!CanRetainSvsmUnboundedDeformingCaster(
            true, false, true, true,
            false, false, false, false));

        constexpr std::array<SvsmObjectInvalidationMode, 4u>
            invalidationModes = {
                SvsmObjectInvalidationMode::Auto,
                SvsmObjectInvalidationMode::Always,
                SvsmObjectInvalidationMode::Rigid,
                SvsmObjectInvalidationMode::Static
            };
        for (const SvsmObjectInvalidationMode mode :
            invalidationModes)
        {
            for (uint32_t eventMask = 0u;
                eventMask < 64u;
                ++eventMask)
            {
                const bool transformChanged =
                    (eventMask & 1u) != 0u;
                const bool materialChanged =
                    (eventMask & 2u) != 0u;
                const bool deformationChanged =
                    (eventMask & 4u) != 0u;
                const bool topologyChanged =
                    (eventMask & 8u) != 0u;
                const bool staticClassificationChanged =
                    (eventMask & 16u) != 0u;
                const bool invalidationModeChanged =
                    (eventMask & 32u) != 0u;
                const bool policyExpected =
                    topologyChanged ||
                    materialChanged ||
                    mode == SvsmObjectInvalidationMode::Always ||
                    (mode == SvsmObjectInvalidationMode::Rigid &&
                        transformChanged) ||
                    (mode == SvsmObjectInvalidationMode::Auto &&
                        (transformChanged ||
                            deformationChanged));
                assert(ShouldInvalidateSvsmObject(
                    mode,
                    transformChanged,
                    materialChanged,
                    deformationChanged,
                    topologyChanged) == policyExpected);

                SvsmCasterEvent exhaustiveEvent;
                exhaustiveEvent.transformChanged =
                    transformChanged;
                exhaustiveEvent.depthMaterialChanged =
                    materialChanged;
                exhaustiveEvent.deformationChanged =
                    deformationChanged;
                exhaustiveEvent.topologyChanged =
                    topologyChanged;
                exhaustiveEvent.invalidationModeChanged =
                    invalidationModeChanged;
                exhaustiveEvent.staticClassificationChanged =
                    staticClassificationChanged;
                const SvsmCasterEventDecision exhaustiveDecision =
                    ReconcileSvsmCasterEvent(
                        mode,
                        exhaustiveEvent);
                const bool decisionExpected =
                    policyExpected ||
                    invalidationModeChanged ||
                    staticClassificationChanged;
                if (decisionExpected)
                {
                    assert(exhaustiveDecision.category ==
                        SvsmCasterEventCategory::Invalidating);
                    assert(exhaustiveDecision.publishedAction ==
                        SvsmPublishedCasterAction::PublishCurrent);
                    assert(exhaustiveDecision.
                        invalidatePreviousCoverage);
                    assert(exhaustiveDecision.
                        invalidateCurrentCoverage);
                }
                else if (transformChanged ||
                    materialChanged ||
                    deformationChanged)
                {
                    assert(exhaustiveDecision.category ==
                        SvsmCasterEventCategory::PolicySuppressed);
                    assert(exhaustiveDecision.publishedAction ==
                        SvsmPublishedCasterAction::Retain);
                }
                else
                {
                    assert(exhaustiveDecision.category ==
                        SvsmCasterEventCategory::Unchanged);
                }
            }
        }

        constexpr uint32_t MissingPublishedState = 0u;
        constexpr uint32_t PublishedStateA = 1u;
        constexpr uint32_t ObservedStateB = 2u;
        const auto applyPublishedDecision =
            [=](uint32_t publishedState,
                uint32_t currentState,
                const SvsmCasterEventDecision& decision,
                bool transactionSucceeded) {
                if (!transactionSucceeded)
                    return publishedState;
                switch (decision.publishedAction)
                {
                case SvsmPublishedCasterAction::PublishCurrent:
                    return currentState;
                case SvsmPublishedCasterAction::Remove:
                    return MissingPublishedState;
                case SvsmPublishedCasterAction::Retain:
                default:
                    return publishedState;
                }
            };

        SvsmCasterEvent event;
        event.transformChanged = true;
        SvsmCasterEventDecision decision =
            ReconcileSvsmCasterEvent(
                SvsmObjectInvalidationMode::Static,
                event);
        assert(decision.category ==
            SvsmCasterEventCategory::PolicySuppressed);
        assert(decision.sceneEventPresent);
        uint32_t publishedState = applyPublishedDecision(
            PublishedStateA,
            ObservedStateB,
            decision,
            true);
        assert(publishedState == PublishedStateA);

        SvsmCasterEvent removedEvent;
        removedEvent.previousExists = true;
        removedEvent.currentExists = false;
        decision = ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Static,
            removedEvent);
        assert(decision.category ==
            SvsmCasterEventCategory::Invalidating);
        assert(decision.invalidatePreviousCoverage);
        assert(!decision.invalidateCurrentCoverage);
        for (const SvsmObjectInvalidationMode mode :
            invalidationModes)
        {
            const SvsmCasterEventDecision authoritativeRemoval =
                ReconcileSvsmCasterEvent(mode, removedEvent);
            assert(authoritativeRemoval.category ==
                SvsmCasterEventCategory::Invalidating);
            assert(authoritativeRemoval.publishedAction ==
                SvsmPublishedCasterAction::Remove);
        }
        publishedState = applyPublishedDecision(
            publishedState,
            MissingPublishedState,
            decision,
            true);
        assert(publishedState == MissingPublishedState);

        event = {};
        event.transformChanged = true;
        decision = ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Static,
            event);
        publishedState = applyPublishedDecision(
            PublishedStateA,
            ObservedStateB,
            decision,
            true);
        assert(publishedState == PublishedStateA);
        decision = ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Auto,
            event);
        assert(decision.category ==
            SvsmCasterEventCategory::Invalidating);
        assert(decision.invalidatePreviousCoverage);
        assert(decision.invalidateCurrentCoverage);
        publishedState = applyPublishedDecision(
            publishedState,
            ObservedStateB,
            decision,
            true);
        assert(publishedState == ObservedStateB);

        // A policy-suppressed A -> B -> C sequence can be rasterized into
        // otherwise dirty pages between observations. The debt marker must
        // survive those commits and force a full refresh when removal or a
        // policy transition next makes the caster authoritative.
        bool suppressionDebt = false;
        const SvsmCasterEventDecision suppressedB =
            ReconcileSvsmCasterEvent(
                SvsmObjectInvalidationMode::Static,
                event);
        bool pendingSuppressionDebt =
            ShouldAccumulateSvsmSuppressedCoverageDebt(
                suppressionDebt,
                suppressedB.category);
        assert(pendingSuppressionDebt);
        assert(!CommitSvsmSuppressedCoverageDebt(
            suppressionDebt,
            pendingSuppressionDebt,
            false,
            false));
        suppressionDebt = CommitSvsmSuppressedCoverageDebt(
            suppressionDebt,
            pendingSuppressionDebt,
            false,
            true);
        assert(suppressionDebt);

        const SvsmCasterEventDecision interveningPageRender =
            ReconcileSvsmCasterEvent(
                SvsmObjectInvalidationMode::Static,
                SvsmCasterEvent{});
        pendingSuppressionDebt =
            ShouldAccumulateSvsmSuppressedCoverageDebt(
                suppressionDebt,
                interveningPageRender.category);
        suppressionDebt = CommitSvsmSuppressedCoverageDebt(
            suppressionDebt,
            pendingSuppressionDebt,
            false,
            true);
        assert(suppressionDebt);

        SvsmCasterEvent suppressedCEvent;
        suppressedCEvent.transformChanged = true;
        suppressedCEvent.deformationChanged = true;
        const SvsmCasterEventDecision suppressedC =
            ReconcileSvsmCasterEvent(
                SvsmObjectInvalidationMode::Static,
                suppressedCEvent);
        pendingSuppressionDebt =
            ShouldAccumulateSvsmSuppressedCoverageDebt(
                suppressionDebt,
                suppressedC.category);
        suppressionDebt = CommitSvsmSuppressedCoverageDebt(
            suppressionDebt,
            pendingSuppressionDebt,
            false,
            true);
        assert(suppressionDebt);
        assert(RequiresSvsmFullRefreshForSuppressedCoverageDebt(
            suppressionDebt,
            ReconcileSvsmCasterEvent(
                SvsmObjectInvalidationMode::Static,
                removedEvent)));

        for (const SvsmObjectInvalidationMode mode :
            invalidationModes)
        {
            for (uint32_t coincidentMask = 0u;
                coincidentMask < 8u;
                ++coincidentMask)
            {
                SvsmCasterEvent modeTransition;
                modeTransition.transformChanged =
                    (coincidentMask & 1u) != 0u;
                modeTransition.depthMaterialChanged =
                    (coincidentMask & 2u) != 0u;
                modeTransition.deformationChanged =
                    (coincidentMask & 4u) != 0u;
                modeTransition.invalidationModeChanged = true;
                const SvsmCasterEventDecision modeDecision =
                    ReconcileSvsmCasterEvent(
                        mode,
                        modeTransition);
                assert(modeDecision.category ==
                    SvsmCasterEventCategory::Invalidating);
                assert(modeDecision.eventPresent);
                assert(modeDecision.sceneEventPresent ==
                    (coincidentMask != 0u));
                assert(modeDecision.invalidatePreviousCoverage);
                assert(modeDecision.invalidateCurrentCoverage);
                assert(
                    RequiresSvsmFullRefreshForSuppressedCoverageDebt(
                        suppressionDebt,
                        modeDecision));
            }
        }
        assert(CommitSvsmSuppressedCoverageDebt(
            suppressionDebt,
            false,
            true,
            false));
        suppressionDebt = CommitSvsmSuppressedCoverageDebt(
            suppressionDebt,
            false,
            true,
            true);
        assert(!suppressionDebt);

        SvsmCasterEvent addedEvent;
        addedEvent.previousExists = false;
        addedEvent.currentExists = true;
        decision = ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Static,
            addedEvent);
        assert(decision.category ==
            SvsmCasterEventCategory::Invalidating);
        assert(!decision.invalidatePreviousCoverage);
        assert(decision.invalidateCurrentCoverage);
        for (const SvsmObjectInvalidationMode mode :
            invalidationModes)
        {
            const SvsmCasterEventDecision authoritativeAddition =
                ReconcileSvsmCasterEvent(mode, addedEvent);
            assert(authoritativeAddition.category ==
                SvsmCasterEventCategory::Invalidating);
            assert(authoritativeAddition.publishedAction ==
                SvsmPublishedCasterAction::PublishCurrent);
        }
        assert(applyPublishedDecision(
            MissingPublishedState,
            ObservedStateB,
            decision,
            true) == ObservedStateB);

        // Object lifetime changes remain authoritative under every policy,
        // including when they coincide with otherwise suppressible motion or
        // deformation and with a paired-depth layer transition.
        for (const SvsmObjectInvalidationMode mode :
            invalidationModes)
        {
            for (uint32_t eventMask = 0u;
                eventMask < 64u;
                ++eventMask)
            {
                for (uint32_t transition = 0u;
                    transition < 2u;
                    ++transition)
                {
                    const bool adding = transition == 0u;
                    SvsmCasterEvent lifetimeEvent;
                    lifetimeEvent.previousExists = !adding;
                    lifetimeEvent.currentExists = adding;
                    lifetimeEvent.transformChanged =
                        (eventMask & 1u) != 0u;
                    lifetimeEvent.depthMaterialChanged =
                        (eventMask & 2u) != 0u;
                    lifetimeEvent.deformationChanged =
                        (eventMask & 4u) != 0u;
                    lifetimeEvent.topologyChanged =
                        (eventMask & 8u) != 0u;
                    lifetimeEvent.invalidationModeChanged =
                        (eventMask & 16u) != 0u;
                    lifetimeEvent.staticClassificationChanged =
                        (eventMask & 32u) != 0u;
                    const SvsmCasterEventDecision lifetimeDecision =
                        ReconcileSvsmCasterEvent(
                            mode,
                            lifetimeEvent);
                    assert(lifetimeDecision.category ==
                        SvsmCasterEventCategory::Invalidating);
                    assert(lifetimeDecision.sceneEventPresent);
                    assert(lifetimeDecision.publishedAction ==
                        (adding
                            ? SvsmPublishedCasterAction::PublishCurrent
                            : SvsmPublishedCasterAction::Remove));
                    assert(lifetimeDecision.
                        invalidatePreviousCoverage == !adding);
                    assert(lifetimeDecision.
                        invalidateCurrentCoverage == adding);
                }
            }
        }

        SvsmCasterEvent materialEvent;
        materialEvent.depthMaterialChanged = true;
        assert(ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Auto,
            materialEvent).category ==
            SvsmCasterEventCategory::Invalidating);
        assert(ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Rigid,
            materialEvent).category ==
            SvsmCasterEventCategory::Invalidating);
        assert(ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Static,
            materialEvent).category ==
            SvsmCasterEventCategory::Invalidating);
        for (const SvsmObjectInvalidationMode mode :
            invalidationModes)
        {
            decision = ReconcileSvsmCasterEvent(
                mode,
                materialEvent);
            assert(decision.category ==
                SvsmCasterEventCategory::Invalidating);
            assert(decision.publishedAction ==
                SvsmPublishedCasterAction::PublishCurrent);
            assert(decision.invalidatePreviousCoverage);
            assert(decision.invalidateCurrentCoverage);
        }

        SvsmCasterEvent classificationEvent;
        classificationEvent.staticClassificationChanged = true;
        decision = ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Static,
            classificationEvent);
        assert(decision.category ==
            SvsmCasterEventCategory::Invalidating);
        assert(decision.eventPresent);
        assert(!decision.sceneEventPresent);

        // A due dynamic-to-static promotion must publish the new layer state
        // even when its selected object policy would suppress the coincident
        // transform/material/deformation change.
        for (const SvsmObjectInvalidationMode mode :
            invalidationModes)
        {
            for (uint32_t coincidentMask = 0u;
                coincidentMask < 8u;
                ++coincidentMask)
            {
                SvsmCasterEvent promotionEvent;
                promotionEvent.transformChanged =
                    (coincidentMask & 1u) != 0u;
                promotionEvent.depthMaterialChanged =
                    (coincidentMask & 2u) != 0u;
                promotionEvent.deformationChanged =
                    (coincidentMask & 4u) != 0u;
                promotionEvent.staticClassificationChanged = true;
                const SvsmCasterEventDecision promotionDecision =
                    ReconcileSvsmCasterEvent(mode, promotionEvent);
                assert(promotionDecision.category ==
                    SvsmCasterEventCategory::Invalidating);
                assert(promotionDecision.publishedAction ==
                    SvsmPublishedCasterAction::PublishCurrent);
                assert(promotionDecision.invalidatePreviousCoverage);
                assert(promotionDecision.invalidateCurrentCoverage);
                assert(applyPublishedDecision(
                    PublishedStateA,
                    ObservedStateB,
                    promotionDecision,
                    true) == ObservedStateB);
                assert(applyPublishedDecision(
                    PublishedStateA,
                    ObservedStateB,
                    promotionDecision,
                    false) == PublishedStateA);
            }
        }

        SvsmCasterEvent bindingOnlyEvent;
        bindingOnlyEvent.bindingOnlyMaterialChanged = true;
        decision = ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Auto,
            bindingOnlyEvent);
        assert(decision.category ==
            SvsmCasterEventCategory::BindingOnly);
        assert(decision.sceneEventPresent);
        assert(decision.publishedAction ==
            SvsmPublishedCasterAction::Retain);

        SvsmCasterEvent unreliableEvent;
        unreliableEvent.transformChanged = true;
        unreliableEvent.reliable = false;
        decision = ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Auto,
            unreliableEvent);
        assert(decision.category ==
            SvsmCasterEventCategory::Unexplained);

        SvsmCasterEvent alwaysEvent;
        decision = ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Always,
            alwaysEvent);
        assert(decision.category ==
            SvsmCasterEventCategory::Invalidating);
        assert(!decision.eventPresent);
        assert(!decision.sceneEventPresent);
        assert(decision.invalidatePreviousCoverage);
        assert(decision.invalidateCurrentCoverage);

        SvsmCasterEvent rigidCombinedEvent;
        rigidCombinedEvent.transformChanged = true;
        rigidCombinedEvent.depthMaterialChanged = true;
        assert(ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Rigid,
            rigidCombinedEvent).category ==
            SvsmCasterEventCategory::Invalidating);

        decision = ReconcileSvsmCasterEvent(
            SvsmObjectInvalidationMode::Auto,
            event);
        assert(applyPublishedDecision(
            PublishedStateA,
            ObservedStateB,
            decision,
            false) == PublishedStateA);
        assert(!IsSvsmObservedSceneChangeExplained(
            true, false, false, true));
        assert(!IsSvsmObservedSceneChangeExplained(
            true, true, true, true));
        assert(IsSvsmObservedSceneChangeExplained(
            true, true, false, true));
        assert(IsSvsmObservedSceneChangeExplained(
            false, false, true, false));
        // A recognized transform on caster A cannot prove that a simultaneous
        // same-handle buffer/texture mutation on caster B was represented.
        // The caller's unassignable dirty channel must veto localization.
        assert(!IsSvsmObservedSceneChangeExplained(
            true, true, false, false));

        assert(!GetEffectiveSvsmDepthBindingCacheReset(
            false, false, true,
            0x1234u, 17u,
            0x1234u, 17u));
        assert(GetEffectiveSvsmDepthBindingCacheReset(
            true, false, true,
            0x1234u, 17u,
            0x1234u, 17u));
        assert(GetEffectiveSvsmDepthBindingCacheReset(
            false, true, true,
            0x1234u, 17u,
            0x1234u, 17u));
        assert(GetEffectiveSvsmDepthBindingCacheReset(
            false, false, false,
            0x1234u, 17u,
            0x1234u, 17u));
        assert(GetEffectiveSvsmDepthBindingCacheReset(
            false, false, true,
            0x1234u, 17u,
            0x5678u, 17u));
        assert(GetEffectiveSvsmDepthBindingCacheReset(
            false, false, true,
            0x1234u, 17u,
            0x1234u, 18u));

        assert(CanUseSvsmLocalizedInvalidation(
            true, true, true, true, true, true,
            true, true, true, false, false));
        assert(!CanUseSvsmLocalizedInvalidation(
            true, true, true, true, true, true,
            true, false, true, false, false));
        assert(!CanUseSvsmLocalizedInvalidation(
            true, true, true, true, true, true,
            true, true, true, true, false));
        assert(!CanUseSvsmLocalizedInvalidation(
            true, true, true, true, true, true,
            true, true, true, false, true));
        assert(ShouldUpgradeSvsmLocalizedPagesToStatic(
            true, false));
        assert(!ShouldUpgradeSvsmLocalizedPagesToStatic(
            true, true));
        assert(!ShouldUpgradeSvsmLocalizedPagesToStatic(
            false, false));
        assert(ShouldInvalidateSvsmCasterSnapshotsOnSuccess(
            false, true));
        assert(ShouldInvalidateSvsmCasterSnapshotsOnSuccess(
            true, false));
        assert(!ShouldInvalidateSvsmCasterSnapshotsOnSuccess(
            true, true));
        assert(
            ShouldBlockSvsmStaticZeroWorkForSnapshotTransaction(
                true, false, false));
        assert(
            ShouldBlockSvsmStaticZeroWorkForSnapshotTransaction(
                false, true, false));
        assert(
            ShouldBlockSvsmStaticZeroWorkForSnapshotTransaction(
                false, false, true));
        assert(
            !ShouldBlockSvsmStaticZeroWorkForSnapshotTransaction(
                false, false, false));

        SvsmAdaptiveCasterClassification classification =
            AdvanceSvsmAdaptiveCasterClassification(
                true, true, true, true,
                SvsmDynamicToStaticPromotionFrames + 1u);
        assert(!classification.staticCacheCandidate);
        assert(classification.stableFrameCount == 0u);
        for (uint32_t frame = 0u;
            frame < SvsmDynamicToStaticPromotionFrames;
            ++frame)
        {
            classification =
                AdvanceSvsmAdaptiveCasterClassification(
                    true,
                    true,
                    false,
                    classification.staticCacheCandidate,
                    classification.stableFrameCount);
            assert(!classification.staticCacheCandidate);
        }
        classification =
            AdvanceSvsmAdaptiveCasterClassification(
                true,
                true,
                false,
                classification.staticCacheCandidate,
                classification.stableFrameCount);
        assert(classification.staticCacheCandidate);
        assert(classification.stableFrameCount ==
            SvsmDynamicToStaticPromotionFrames + 1u);
        const auto ineligible =
            AdvanceSvsmAdaptiveCasterClassification(
                true, false, false, true, 50u);
        assert(!ineligible.staticCacheCandidate);
        assert(ineligible.stableFrameCount == 0u);
        const auto referenceClassification =
            AdvanceSvsmAdaptiveCasterClassification(
                false, true, true, false, 0u);
        assert(referenceClassification.staticCacheCandidate);

        constexpr uint64_t demotionCommitCount = 37u;
        constexpr uint64_t promotionDeadline =
            GetSvsmDynamicCasterPromotionDeadline(
                demotionCommitCount);
        static_assert(
            promotionDeadline ==
                demotionCommitCount +
                    SvsmDynamicToStaticPromotionFrames + 1u);
        assert(!IsSvsmDynamicCasterPromotionDue(
            demotionCommitCount +
                SvsmDynamicToStaticPromotionFrames,
            promotionDeadline));
        assert(IsSvsmDynamicCasterPromotionDue(
            demotionCommitCount +
                SvsmDynamicToStaticPromotionFrames + 1u,
            promotionDeadline));
        const auto deadlineDemotion =
            AdvanceSvsmAdaptiveCasterDeadlineClassification(
                true,
                true,
                true,
                true,
                SvsmNoPromotionDeadline,
                demotionCommitCount);
        assert(!deadlineDemotion.staticCacheCandidate);
        assert(deadlineDemotion.promotionDeadline ==
            promotionDeadline);
        const auto deadlineWaiting =
            AdvanceSvsmAdaptiveCasterDeadlineClassification(
                true,
                true,
                false,
                false,
                deadlineDemotion.promotionDeadline,
                promotionDeadline - 1u);
        assert(!deadlineWaiting.staticCacheCandidate);
        assert(deadlineWaiting.promotionDeadline ==
            promotionDeadline);
        const auto deadlinePromoted =
            AdvanceSvsmAdaptiveCasterDeadlineClassification(
                true,
                true,
                false,
                false,
                deadlineWaiting.promotionDeadline,
                promotionDeadline);
        assert(deadlinePromoted.staticCacheCandidate);
        assert(deadlinePromoted.promotionDeadline ==
            SvsmNoPromotionDeadline);
    }

    void TestDeferredStaticDepthMerge()
    {
        SvsmDeferredStaticDepthMergePageState eligible;
        eligible.deferredMergeEnabled = true;
        eligible.pairedDepthActive = true;
        eligible.resident = true;
        eligible.dirty = true;
        eligible.staticDirty = true;
        eligible.ownerValid = true;
        eligible.ownerClipmapMatches = true;
        eligible.physicalPageInRange = true;
        eligible.pagePhysicalMatches = true;
        eligible.physicalOwnerMatches = true;
        eligible.renderPageScheduled = true;
        assert(ShouldMergeSvsmDeferredStaticDepthPage(eligible));

        constexpr std::array<
            bool SvsmDeferredStaticDepthMergePageState::*,
            11u> requiredPredicates = {
            &SvsmDeferredStaticDepthMergePageState::
                deferredMergeEnabled,
            &SvsmDeferredStaticDepthMergePageState::
                pairedDepthActive,
            &SvsmDeferredStaticDepthMergePageState::resident,
            &SvsmDeferredStaticDepthMergePageState::dirty,
            &SvsmDeferredStaticDepthMergePageState::staticDirty,
            &SvsmDeferredStaticDepthMergePageState::ownerValid,
            &SvsmDeferredStaticDepthMergePageState::
                ownerClipmapMatches,
            &SvsmDeferredStaticDepthMergePageState::
                physicalPageInRange,
            &SvsmDeferredStaticDepthMergePageState::
                pagePhysicalMatches,
            &SvsmDeferredStaticDepthMergePageState::
                physicalOwnerMatches,
            &SvsmDeferredStaticDepthMergePageState::
                renderPageScheduled
        };
        for (const auto predicate : requiredPredicates)
        {
            SvsmDeferredStaticDepthMergePageState rejected =
                eligible;
            rejected.*predicate = false;
            assert(!ShouldMergeSvsmDeferredStaticDepthPage(
                rejected));
        }

        // A false render-page match models every unscheduled cause, including
        // finite-budget rejection and an omitted compact-list entry.
        SvsmDeferredStaticDepthMergePageState overBudget = eligible;
        overBudget.renderPageScheduled = false;
        assert(!ShouldMergeSvsmDeferredStaticDepthPage(overBudget));

        assert(GetSvsmStaticDepthPostRasterWork(false, false) ==
            SvsmStaticDepthPostRasterWork::None);
        assert(GetSvsmStaticDepthPostRasterWork(true, false) ==
            SvsmStaticDepthPostRasterWork::MergeOnly);
        assert(GetSvsmStaticDepthPostRasterWork(false, true) ==
            SvsmStaticDepthPostRasterWork::HierarchyOnly);
        assert(GetSvsmStaticDepthPostRasterWork(true, true) ==
            SvsmStaticDepthPostRasterWork::MergeAndHierarchy);

        assert(IsSvsmDeferredStaticDepthMergeActive(
            true, true, true, true));
        assert(!IsSvsmDeferredStaticDepthMergeActive(
            false, true, true, true));
        assert(!IsSvsmDeferredStaticDepthMergeActive(
            true, false, true, true));
        assert(!IsSvsmDeferredStaticDepthMergeActive(
            true, true, false, true));
        assert(!IsSvsmDeferredStaticDepthMergeActive(
            true, true, true, false));

        constexpr uint32_t staticDepth = 0x3f000000u;
        constexpr uint32_t nearerDynamicDepth = 0x3f400000u;
        const SvsmPairedDepthValues legacyStatic =
            ApplySvsmPairedDepthFragment(
                {}, false, true, true, staticDepth);
        assert(legacyStatic.merged == staticDepth);
        assert(legacyStatic.staticDepth == staticDepth);
        const SvsmPairedDepthValues deferredStatic =
            ApplySvsmPairedDepthFragment(
                {}, true, true, true, staticDepth);
        assert(deferredStatic.merged == 0u);
        assert(deferredStatic.staticDepth == staticDepth);
        const SvsmPairedDepthValues deferredDynamic =
            ApplySvsmPairedDepthFragment(
                deferredStatic,
                true,
                true,
                false,
                nearerDynamicDepth);
        assert(deferredDynamic.merged == nearerDynamicDepth);
        assert(deferredDynamic.staticDepth == staticDepth);
        const SvsmPairedDepthValues merged =
            FinishSvsmDeferredStaticDepthMerge(
                deferredDynamic, true, true, true);
        assert(merged.merged == nearerDynamicDepth);
        assert(merged.staticDepth == staticDepth);
        assert(FinishSvsmDeferredStaticDepthMerge(
                deferredStatic, true, true, true).merged ==
            staticDepth);
        assert(FinishSvsmDeferredStaticDepthMerge(
                deferredStatic, true, true, false).merged == 0u);

        // Effective-unpaired moving-light mode retains the ordinary slice-zero
        // raster even when the configured optimization permutation is active.
        const SvsmPairedDepthValues movingLightStatic =
            ApplySvsmPairedDepthFragment(
                {}, true, false, true, staticDepth);
        assert(movingLightStatic.merged == staticDepth);
        assert(movingLightStatic.staticDepth == 0u);

        // Deterministic randomized equivalence across arbitrary static and
        // dynamic fragment orderings. The legacy path writes two atomics for
        // each static fragment; deferred mode writes one and performs one
        // page merge after raster.
        uint32_t randomState = 0x6d2b79f5u;
        for (uint32_t trial = 0u; trial < 1024u; ++trial)
        {
            SvsmPairedDepthValues legacy;
            SvsmPairedDepthValues deferred;
            randomState =
                randomState * 1664525u + 1013904223u;
            const uint32_t fragmentCount =
                1u + (randomState & 63u);
            for (uint32_t fragment = 0u;
                fragment < fragmentCount;
                ++fragment)
            {
                randomState =
                    randomState * 1664525u + 1013904223u;
                const bool staticCaster =
                    (randomState & 1u) != 0u;
                randomState =
                    randomState * 1664525u + 1013904223u;
                const uint32_t reverseDepth = randomState;
                legacy = ApplySvsmPairedDepthFragment(
                    legacy,
                    false,
                    true,
                    staticCaster,
                    reverseDepth);
                deferred = ApplySvsmPairedDepthFragment(
                    deferred,
                    true,
                    true,
                    staticCaster,
                    reverseDepth);
            }
            deferred = FinishSvsmDeferredStaticDepthMerge(
                deferred, true, true, true);
            assert(deferred.merged == legacy.merged);
            assert(deferred.staticDepth == legacy.staticDepth);
        }
    }

    void TestMovingLightCachePolicy()
    {
        const SvsmMovingLightFramePolicy initial =
            GetSvsmMovingLightFramePolicy(
                true, true, true, true, true, 0u, 10u);
        assert(!initial.effectiveCacheEnabled);
        assert(!initial.effectivePairedDepthEnabled);
        assert(initial.forceContentInvalidation);
        assert(initial.uncached);
        assert(!initial.transitioningToCached);
        assert(initial.previousUncachedAfterCommit);
        assert(initial.recoveryFramesAfterCommit == 10u);
        assert(std::abs(initial.lodRecoveryFactor - 1.f) < 1e-6f);

        // A failed sparse attempt does not mutate the committed inputs. The
        // exact same policy is therefore selected on retry.
        const SvsmMovingLightFramePolicy failedRetry =
            GetSvsmMovingLightFramePolicy(
                true, true, true, true, true, 0u, 10u);
        assert(failedRetry.uncached == initial.uncached);
        assert(failedRetry.recoveryFramesAfterCommit ==
            initial.recoveryFramesAfterCommit);
        assert(failedRetry.previousUncachedAfterCommit ==
            initial.previousUncachedAfterCommit);

        const SvsmMovingLightFramePolicy firstUnchanged =
            GetSvsmMovingLightFramePolicy(
                true,
                true,
                true,
                false,
                initial.previousUncachedAfterCommit,
                initial.recoveryFramesAfterCommit,
                10u);
        assert(firstUnchanged.effectiveCacheEnabled);
        assert(firstUnchanged.effectivePairedDepthEnabled);
        assert(firstUnchanged.forceContentInvalidation);
        assert(!firstUnchanged.uncached);
        assert(firstUnchanged.transitioningToCached);
        assert(!firstUnchanged.previousUncachedAfterCommit);
        assert(firstUnchanged.recoveryFramesAfterCommit == 9u);
        assert(std::abs(
            firstUnchanged.lodRecoveryFactor - 0.9f) < 1e-6f);

        const SvsmMovingLightFramePolicy secondUnchanged =
            GetSvsmMovingLightFramePolicy(
                true,
                true,
                true,
                false,
                firstUnchanged.previousUncachedAfterCommit,
                firstUnchanged.recoveryFramesAfterCommit,
                10u);
        assert(secondUnchanged.effectiveCacheEnabled);
        assert(secondUnchanged.effectivePairedDepthEnabled);
        assert(!secondUnchanged.forceContentInvalidation);
        assert(!secondUnchanged.transitioningToCached);
        assert(secondUnchanged.recoveryFramesAfterCommit == 8u);
        assert(std::abs(
            secondUnchanged.lodRecoveryFactor - 0.8f) < 1e-6f);

        const SvsmMovingLightFramePolicy continuousMotion =
            GetSvsmMovingLightFramePolicy(
                true,
                true,
                true,
                true,
                secondUnchanged.previousUncachedAfterCommit,
                secondUnchanged.recoveryFramesAfterCommit,
                10u);
        assert(continuousMotion.uncached);
        assert(!continuousMotion.effectivePairedDepthEnabled);
        assert(continuousMotion.recoveryFramesAfterCommit == 10u);

        const SvsmMovingLightFramePolicy disableDuringMotion =
            GetSvsmMovingLightFramePolicy(
                false, true, true, false, true, 10u, 10u);
        assert(disableDuringMotion.effectiveCacheEnabled);
        assert(disableDuringMotion.effectivePairedDepthEnabled);
        assert(disableDuringMotion.forceContentInvalidation);
        assert(disableDuringMotion.transitioningToCached);
        assert(!disableDuringMotion.previousUncachedAfterCommit);
        const SvsmMovingLightFramePolicy disabledStable =
            GetSvsmMovingLightFramePolicy(
                false, true, true, false, false, 0u, 10u);
        assert(disabledStable.effectiveCacheEnabled);
        assert(disabledStable.effectivePairedDepthEnabled);
        assert(!disabledStable.forceContentInvalidation);

        const SvsmMovingLightFramePolicy cacheDisabled =
            GetSvsmMovingLightFramePolicy(
                true, false, true, true, false, 0u, 10u);
        assert(!cacheDisabled.effectiveCacheEnabled);
        assert(!cacheDisabled.effectivePairedDepthEnabled);
        assert(!cacheDisabled.forceContentInvalidation);
        assert(cacheDisabled.previousUncachedAfterCommit);
        const SvsmMovingLightFramePolicy cacheReenabled =
            GetSvsmMovingLightFramePolicy(
                true,
                true,
                true,
                false,
                cacheDisabled.previousUncachedAfterCommit,
                cacheDisabled.recoveryFramesAfterCommit,
                10u);
        assert(cacheReenabled.effectiveCacheEnabled);
        assert(cacheReenabled.effectivePairedDepthEnabled);
        assert(cacheReenabled.forceContentInvalidation);
        assert(cacheReenabled.transitioningToCached);
        const SvsmMovingLightFramePolicy pairedDisabled =
            GetSvsmMovingLightFramePolicy(
                true, true, false, false, true, 10u, 10u);
        assert(pairedDisabled.effectiveCacheEnabled);
        assert(!pairedDisabled.effectivePairedDepthEnabled);
        assert(pairedDisabled.forceContentInvalidation);

        assert(GetEffectiveSvsmMovingLightResolutionBias(
            SvsmResolutionBias::Zero,
            true,
            SvsmResolutionBias::PlusOne,
            1.f) == SvsmResolutionBias::PlusOne);
        assert(GetEffectiveSvsmMovingLightResolutionBias(
            SvsmResolutionBias::Zero,
            true,
            SvsmResolutionBias::PlusOne,
            0.9f) == SvsmResolutionBias::PlusOne);
        assert(GetEffectiveSvsmMovingLightResolutionBias(
            SvsmResolutionBias::Zero,
            true,
            SvsmResolutionBias::PlusOne,
            0.1f) == SvsmResolutionBias::PlusOne);
        assert(GetEffectiveSvsmMovingLightResolutionBias(
            SvsmResolutionBias::Zero,
            true,
            SvsmResolutionBias::PlusOne,
            0.f) == SvsmResolutionBias::Zero);
        assert(GetEffectiveSvsmMovingLightResolutionBias(
            SvsmResolutionBias::Zero,
            true,
            SvsmResolutionBias::PlusTwo,
            0.9f) == SvsmResolutionBias::PlusTwo);
        assert(GetEffectiveSvsmMovingLightResolutionBias(
            SvsmResolutionBias::Zero,
            true,
            SvsmResolutionBias::PlusTwo,
            0.4f) == SvsmResolutionBias::PlusOne);
        assert(GetEffectiveSvsmMovingLightResolutionBias(
            SvsmResolutionBias::PlusOne,
            true,
            SvsmResolutionBias::PlusTwo,
            1.f) == SvsmResolutionBias::PlusTwo);
        assert(GetEffectiveSvsmMovingLightResolutionBias(
            SvsmResolutionBias::PlusOne,
            false,
            SvsmResolutionBias::PlusTwo,
            1.f) == SvsmResolutionBias::PlusOne);

        SvsmPageMetadata resident;
        resident.physicalPage = 42u;
        resident.age = 77u;
        resident.resident = true;
        resident.required = true;
        resident.dirty = false;
        resident.staticDirty = true;
        const SvsmContentInvalidationResult retainedPaired =
            InvalidateSvsmPageContent(
                resident, 4096u, true, true, true);
        assert(retainedPaired.retainedPhysicalMapping);
        assert(!retainedPaired.releasePhysicalOwner);
        assert(retainedPaired.metadata.physicalPage == 42u);
        assert(retainedPaired.metadata.age == 77u);
        assert(retainedPaired.metadata.resident);
        assert(!retainedPaired.metadata.required);
        assert(retainedPaired.metadata.dirty);
        assert(retainedPaired.metadata.staticDirty);

        const SvsmContentInvalidationResult retainedUnpaired =
            InvalidateSvsmPageContent(
                resident, 4096u, true, true, false);
        assert(retainedUnpaired.retainedPhysicalMapping);
        assert(!retainedUnpaired.metadata.staticDirty);

        const SvsmContentInvalidationResult mismatchedOwner =
            InvalidateSvsmPageContent(
                resident, 4096u, false, true, true);
        assert(!mismatchedOwner.retainedPhysicalMapping);
        assert(!mismatchedOwner.releasePhysicalOwner);
        assert(!mismatchedOwner.metadata.resident);
        assert(mismatchedOwner.metadata.dirty);
        assert(mismatchedOwner.metadata.staticDirty);

        const SvsmContentInvalidationResult destructive =
            InvalidateSvsmPageContent(
                resident, 4096u, true, false, true);
        assert(!destructive.retainedPhysicalMapping);
        assert(destructive.releasePhysicalOwner);
        assert(!destructive.metadata.resident);
        assert(destructive.metadata.dirty);
        assert(destructive.metadata.staticDirty);

        resident.physicalPage = 4096u;
        const SvsmContentInvalidationResult outOfRange =
            InvalidateSvsmPageContent(
                resident, 4096u, true, true, true);
        assert(!outOfRange.retainedPhysicalMapping);
        assert(!outOfRange.releasePhysicalOwner);
        assert(!outOfRange.metadata.resident);

        assert(GetSvsmPhysicalDepthArraySize(false) == 1u);
        assert(GetSvsmPhysicalDepthArraySize(true) == 2u);
        assert(ShouldRenderSvsmPacketCaster(
            false, true, true, true, false));
        assert(ShouldRenderSvsmPacketCaster(
            true, true, true, true, true));
        assert(!ShouldRenderSvsmPacketCaster(
            true, true, true, true, false));
        assert(ShouldRenderSvsmPacketCaster(
            true, false, true, true, false));
        assert(!ShouldRenderSvsmPacketCaster(
            false, true, true, false, false));
        assert(ShouldWriteSvsmStaticDepth(true, true));
        assert(!ShouldWriteSvsmStaticDepth(false, true));
        assert(!ShouldWriteSvsmStaticDepth(true, false));
    }

    void TestReceiverDistanceMipClamp()
    {
        const float stationaryStart =
            GetEffectiveSvsmReceiverDistanceMipClampStart(
                true,
                20.f,
                1.5f,
                true,
                true,
                SvsmResolutionBias::PlusOne,
                0.f);
        assert(std::abs(stationaryStart - 30.f) < 1e-6f);
        const float movingStart =
            GetEffectiveSvsmReceiverDistanceMipClampStart(
                true,
                20.f,
                1.5f,
                true,
                true,
                SvsmResolutionBias::PlusOne,
                1.f);
        assert(std::abs(movingStart - 15.f) < 1e-6f);
        const float halfwayRecoveredStart =
            GetEffectiveSvsmReceiverDistanceMipClampStart(
                true,
                20.f,
                1.5f,
                true,
                true,
                SvsmResolutionBias::PlusOne,
                0.5f);
        assert(std::abs(
            halfwayRecoveredStart -
                30.f / std::sqrt(2.f)) < 1e-5f);
        const float twoLevelMovingStart =
            GetEffectiveSvsmReceiverDistanceMipClampStart(
                true,
                20.f,
                1.5f,
                true,
                true,
                SvsmResolutionBias::PlusTwo,
                1.f);
        assert(std::abs(twoLevelMovingStart - 7.5f) < 1e-6f);
        assert(GetEffectiveSvsmReceiverDistanceMipClampStart(
            false,
            20.f,
            1.5f,
            true,
            true,
            SvsmResolutionBias::PlusOne,
            1.f) == 0.f);
        assert(GetEffectiveSvsmReceiverDistanceMipClampStart(
            true,
            std::numeric_limits<float>::quiet_NaN(),
            1.5f,
            true,
            true,
            SvsmResolutionBias::PlusOne,
            1.f) == 0.f);
        assert(GetEffectiveSvsmReceiverDistanceMipClampStart(
            true,
            20.f,
            1.5f,
            true,
            true,
            SvsmResolutionBias::PlusOne,
            std::numeric_limits<float>::quiet_NaN()) ==
            stationaryStart);

        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::Zero, 29.999f, 30.f, 4u) == 0u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::Zero, 30.f, 30.f, 4u) == 1u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::Zero, 59.999f, 30.f, 4u) == 1u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::Zero, 60.f, 30.f, 4u) == 2u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::Zero, 120.f, 30.f, 4u) == 3u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::Zero, 240.f, 30.f, 4u) == 4u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::Zero, 10000.f, 30.f, 4u) == 4u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::Zero, 10000.f, 30.f, 2u) == 2u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::PlusTwo, 0.f, 30.f, 4u) == 2u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::PlusTwo, 10000.f, 30.f, 0u) == 2u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::PlusOne,
            std::numeric_limits<float>::quiet_NaN(),
            30.f,
            4u) == 1u);
        assert(GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::PlusOne, 100.f, 0.f, 4u) == 1u);

        // Tiled marking starts at the nearest receiver's level and now
        // continues through the farthest receiver's forced first level.
        // This makes the requested level interval a superset of every
        // per-pixel distance-clamped first level in Tile8 and Tile16.
        const std::array<float, 8u> tileDistances = {
            95.f, 31.f, 240.f, 63.f, 180.f, 45.f, 500.f, 87.f
        };
        const float tileMinimumDistance = *std::min_element(
            tileDistances.begin(), tileDistances.end());
        const float tileMaximumDistance = *std::max_element(
            tileDistances.begin(), tileDistances.end());
        const uint32_t tiledLevel = GetSvsmReceiverFirstClipmapLevel(
            SvsmResolutionBias::Zero,
            tileMinimumDistance,
            30.f,
            4u);
        const uint32_t tiledMaximumLevel =
            GetSvsmReceiverFirstClipmapLevel(
                SvsmResolutionBias::Zero,
                tileMaximumDistance,
                30.f,
                4u);
        for (float distance : tileDistances)
        {
            const uint32_t pixelLevel =
                GetSvsmReceiverFirstClipmapLevel(
                SvsmResolutionBias::Zero,
                distance,
                30.f,
                4u);
            assert(tiledLevel <= pixelLevel);
            assert(pixelLevel <= tiledMaximumLevel);
        }
        assert(ShouldContinueSvsmTiledReceiverLodMarking(
            tiledLevel, tiledMaximumLevel, true));
        assert(ShouldContinueSvsmTiledReceiverLodMarking(
            tiledMaximumLevel - 1u, tiledMaximumLevel, true));
        assert(!ShouldContinueSvsmTiledReceiverLodMarking(
            tiledMaximumLevel, tiledMaximumLevel, true));
        assert(ShouldContinueSvsmTiledReceiverLodMarking(
            tiledMaximumLevel, tiledMaximumLevel, false));

        auto assertTiledRequestSuperset =
            [](const auto& distances) {
                const float minimumDistance = *std::min_element(
                    distances.begin(), distances.end());
                const float maximumDistance = *std::max_element(
                    distances.begin(), distances.end());
                const uint32_t minimumLevel =
                    GetSvsmReceiverFirstClipmapLevel(
                        SvsmResolutionBias::Zero,
                        minimumDistance,
                        30.f,
                        4u);
                const uint32_t maximumLevel =
                    GetSvsmReceiverFirstClipmapLevel(
                        SvsmResolutionBias::Zero,
                        maximumDistance,
                        30.f,
                        4u);
                std::array<bool, SvsmClipmapCount> requested{};
                for (uint32_t level = minimumLevel;
                    level <= maximumLevel;
                    ++level)
                {
                    requested[level] = true;
                }
                requested[SvsmClipmapCount - 1u] = true;
                for (float distance : distances)
                {
                    const uint32_t pixelLevel =
                        GetSvsmReceiverFirstClipmapLevel(
                            SvsmResolutionBias::Zero,
                            distance,
                            30.f,
                            4u);
                    assert(requested[pixelLevel]);
                }
            };
        std::array<float, 64u> tile8Distances{};
        std::array<float, 256u> tile16Distances{};
        constexpr std::array<float, 10u> thresholdDistances = {
            0.f, 29.999f, 30.f, 59.999f, 60.f,
            119.999f, 120.f, 239.999f, 240.f, 1000.f
        };
        for (uint32_t index = 0u;
            index < tile8Distances.size();
            ++index)
        {
            tile8Distances[index] =
                thresholdDistances[index % thresholdDistances.size()];
        }
        for (uint32_t index = 0u;
            index < tile16Distances.size();
            ++index)
        {
            tile16Distances[index] =
                thresholdDistances[
                    (index * 7u) % thresholdDistances.size()];
        }
        assertTiledRequestSuperset(tile8Distances);
        assertTiledRequestSuperset(tile16Distances);

        // Continuous mode and the discrete global reference are mutually
        // exclusive: the moving increment must be spent exactly once.
        assert(GetEffectiveSvsmReceiverAwareMovingLightResolutionBias(
            SvsmResolutionBias::Zero,
            true,
            SvsmResolutionBias::PlusTwo,
            1.f,
            true,
            true) == SvsmResolutionBias::Zero);
        assert(GetEffectiveSvsmReceiverAwareMovingLightResolutionBias(
            SvsmResolutionBias::Zero,
            true,
            SvsmResolutionBias::PlusTwo,
            1.f,
            true,
            false) == SvsmResolutionBias::PlusTwo);
        assert(GetEffectiveSvsmReceiverAwareMovingLightResolutionBias(
            SvsmResolutionBias::PlusOne,
            true,
            SvsmResolutionBias::PlusTwo,
            1.f,
            false,
            true) == SvsmResolutionBias::PlusTwo);

        uint32_t previousLevel = 0u;
        for (uint32_t step = 0u; step <= 4096u; ++step)
        {
            const float distance = float(step) * 0.25f;
            const uint32_t level = GetSvsmReceiverFirstClipmapLevel(
                SvsmResolutionBias::Zero,
                distance,
                30.f,
                4u);
            assert(level >= previousLevel);
            assert(level <=
                SvsmMaximumReceiverDistanceMipClampLevel);
            previousLevel = level;
        }
    }

    void TestFinePageRenderBudgetScheduling()
    {
        constexpr uint32_t coarsestLevel = SvsmClipmapCount - 1u;
        constexpr uint32_t unlimited =
            std::numeric_limits<uint32_t>::max();
        static_assert(SvsmPagesPerClipmap % 32u == 0u);
        static_assert(SvsmFineClipmapCount == 5u);
        static_assert(
            SvsmFinePageCandidateMaskWordsPerLevel == 128u);
        static_assert(SvsmFinePageCandidateMaskWordCount == 640u);
        static_assert(
            SvsmFinePageCandidateMaskWordCount * sizeof(uint32_t) ==
            2560u);

        uint32_t fineReservation = 0u;
        const auto schedule =
            [&fineReservation, coarsestLevel](
                uint32_t clipmapLevel,
                uint32_t budget)
            {
                const uint32_t reservation = fineReservation;
                if (clipmapLevel < SvsmClipmapCount &&
                    clipmapLevel != coarsestLevel)
                {
                    ++fineReservation;
                }
                return ShouldScheduleSvsmDirtyPageRender(
                    clipmapLevel, reservation, budget);
            };

        // Fine levels share one reservation while coarsest work bypasses it.
        assert(schedule(0u, 2u));
        assert(fineReservation == 1u);
        assert(schedule(coarsestLevel, 2u));
        assert(fineReservation == 1u);
        assert(schedule(4u, 2u));
        assert(fineReservation == 2u);
        assert(schedule(coarsestLevel, 2u));
        assert(fineReservation == 2u);
        assert(!schedule(2u, 2u));
        assert(fineReservation == 3u);

        // A finite fine-page budget cannot starve the current-light coarse
        // fallback. More coarse pages than the fine budget must all schedule
        // without consuming its independent reservation.
        fineReservation = 0u;
        for (uint32_t page = 0u; page < 9u; ++page)
            assert(schedule(coarsestLevel, 4u));
        assert(fineReservation == 0u);
        for (uint32_t page = 0u; page < 4u; ++page)
            assert(schedule(4u, 4u));
        assert(!schedule(4u, 4u));

        SparseVirtualShadowMapSettings motionBenchmarkTarget;
        motionBenchmarkTarget.coarsestPageRenderBudgetEnabled = true;
        ApplySvsmFinePageRenderBudget(motionBenchmarkTarget, 4u);
        assert(motionBenchmarkTarget.pageRenderBudget == 4u);
        assert(!motionBenchmarkTarget.coarsestPageRenderBudgetEnabled);
        assert(ShouldUseSvsmDeterministicFinePageBudget(
            4u, 4096u, false));
        assert(ShouldUseSvsmDeterministicFinePageBudget(
            0u, 4096u, false));
        assert(!ShouldUseSvsmDeterministicFinePageBudget(
            unlimited, 4096u, false));
        assert(!ShouldUseSvsmDeterministicFinePageBudget(
            4096u, 4096u, false));
        assert(!ShouldUseSvsmDeterministicFinePageBudget(
            4u, 4096u, true));
        assert(GetSvsmDeterministicFinePageOrderKey(0u, 4095u) <
            GetSvsmDeterministicFinePageOrderKey(1u, 0u));
        constexpr uint32_t centerPage =
            (SvsmPagesPerAxis / 2u) +
            (SvsmPagesPerAxis / 2u) * SvsmPagesPerAxis;
        assert(GetSvsmDeterministicFinePageOrderKey(
            0u, centerPage) == 0u);
        assert(GetSvsmDeterministicFinePageOrderKey(
            0u, centerPage - 1u) == 1u);
        assert(GetSvsmDeterministicFinePageOrderKey(
            0u, centerPage - SvsmPagesPerAxis) == 2u);
        assert(GetSvsmDeterministicFinePageOrderKey(
            0u, centerPage - SvsmPagesPerAxis - 1u) == 3u);
        assert(GetSvsmDeterministicFinePageOrderKey(
            0u, centerPage + 1u) == 4u);
        std::array<bool, SvsmPagesPerClipmap>
            deterministicPageOrderSeen{};
        std::array<uint32_t, SvsmPagesPerClipmap>
            deterministicPageForOrder{};
        for (uint32_t page = 0u;
            page < SvsmPagesPerClipmap;
            ++page)
        {
            const uint32_t key =
                GetSvsmDeterministicFinePageOrderKey(0u, page);
            assert(key < SvsmPagesPerClipmap);
            assert(!deterministicPageOrderSeen[key]);
            deterministicPageOrderSeen[key] = true;
            deterministicPageForOrder[key] = page;
        }
        assert(std::all_of(
            deterministicPageOrderSeen.begin(),
            deterministicPageOrderSeen.end(),
            [](bool seen) { return seen; }));
        assert(GetSvsmDeterministicFinePageOrderKey(
            coarsestLevel, 0u) == unlimited);
        assert(GetSvsmDeterministicFinePageOrderKey(
            0u, SvsmPagesPerClipmap) == unlimited);

        struct DeterministicBudgetCandidate
        {
            uint32_t level;
            uint32_t page;
            bool required;
            bool dirty;
            bool resident;
        };
        const auto selectDeterministicCandidates =
            [](std::vector<DeterministicBudgetCandidate> candidates,
               uint32_t budget,
               uint32_t availableNewResidency)
            {
                std::sort(
                    candidates.begin(),
                    candidates.end(),
                    [](const DeterministicBudgetCandidate& left,
                       const DeterministicBudgetCandidate& right) {
                        return GetSvsmDeterministicFinePageOrderKey(
                                   left.level, left.page) <
                            GetSvsmDeterministicFinePageOrderKey(
                                right.level, right.page);
                    });
                std::vector<uint32_t> keys;
                keys.reserve(std::min<std::size_t>(
                    candidates.size(), budget));
                for (const auto& candidate : candidates)
                {
                    if (keys.size() >= budget)
                        break;
                    if (!candidate.required ||
                        candidate.level >=
                            SvsmClipmapCount - 1u ||
                        candidate.page >=
                            SvsmPagesPerClipmap ||
                        (candidate.resident && !candidate.dirty))
                    {
                        continue;
                    }
                    if (!candidate.resident)
                    {
                        if (availableNewResidency == 0u)
                            continue;
                        --availableNewResidency;
                    }
                    keys.push_back(
                        GetSvsmDeterministicFinePageOrderKey(
                            candidate.level,
                            candidate.page));
                }
                return keys;
            };
        const auto selectDeterministicBitmaskCandidates =
            [](const std::vector<DeterministicBudgetCandidate>& candidates,
               uint32_t budget,
               uint32_t availableNewResidency)
            {
                constexpr uint32_t FineLevelCount =
                    SvsmClipmapCount - 1u;
                constexpr uint32_t MaskWordCount =
                    SvsmPagesPerClipmap / 32u;
                using FineMasks = std::array<
                    std::array<uint32_t, MaskWordCount>,
                    FineLevelCount>;
                FineMasks candidateMasks{};
                std::array<
                    std::array<uint8_t, SvsmPagesPerClipmap>,
                    FineLevelCount> residentStates{};
                for (const auto& candidate : candidates)
                {
                    if (!candidate.required ||
                        candidate.level >= FineLevelCount ||
                        candidate.page >= SvsmPagesPerClipmap ||
                        (candidate.resident && !candidate.dirty))
                    {
                        continue;
                    }
                    const uint32_t orderKey =
                        GetSvsmDeterministicFinePageOrderKey(
                            candidate.level,
                            candidate.page);
                    const uint32_t levelOrder =
                        orderKey -
                        candidate.level * SvsmPagesPerClipmap;
                    const uint32_t word = levelOrder / 32u;
                    const uint32_t bit = 1u << (levelOrder % 32u);
                    candidateMasks[candidate.level][word] |= bit;
                    residentStates[candidate.level][levelOrder] =
                        candidate.resident ? 1u : 0u;
                }

                std::vector<uint32_t> keys;
                keys.reserve(std::min<std::size_t>(
                    candidates.size(), budget));
                for (uint32_t level = 0u;
                    level < FineLevelCount && keys.size() < budget;
                    ++level)
                {
                    for (uint32_t word = 0u;
                        word < MaskWordCount && keys.size() < budget;
                        ++word)
                    {
                        uint32_t bits = candidateMasks[level][word];
                        for (uint32_t bitIndex = 0u;
                            bitIndex < 32u && keys.size() < budget;
                            ++bitIndex)
                        {
                            const uint32_t bit = 1u << bitIndex;
                            if ((bits & bit) == 0u)
                                continue;
                            const bool resident =
                                residentStates[level][
                                    word * 32u + bitIndex] != 0u;
                            if (!resident)
                            {
                                if (availableNewResidency == 0u)
                                    continue;
                                --availableNewResidency;
                            }
                            keys.push_back(
                                level * SvsmPagesPerClipmap +
                                word * 32u + bitIndex);
                        }
                    }
                }
                return keys;
            };
        std::vector<DeterministicBudgetCandidate>
            deterministicCandidates = {
                { 4u, centerPage, true, true, true },
                { 0u, centerPage + 1u, true, true, true },
                { 0u, centerPage, true, false, false },
                { 1u, centerPage, true, true, true },
                { coarsestLevel, centerPage, true, true, true },
                { 0u, centerPage + 2u, true, false, true },
                { 0u, centerPage + 3u, false, true, false },
                { 0u, centerPage + 4u, true, true, false }
            };
        const std::vector<uint32_t> expectedBudgetFour = {
            GetSvsmDeterministicFinePageOrderKey(0u, centerPage),
            GetSvsmDeterministicFinePageOrderKey(
                0u, centerPage + 1u),
            GetSvsmDeterministicFinePageOrderKey(1u, centerPage),
            GetSvsmDeterministicFinePageOrderKey(4u, centerPage)
        };
        assert(selectDeterministicCandidates(
            deterministicCandidates, 4u, 1u) == expectedBudgetFour);
        assert(selectDeterministicBitmaskCandidates(
            deterministicCandidates, 4u, 1u) == expectedBudgetFour);
        std::reverse(
            deterministicCandidates.begin(),
            deterministicCandidates.end());
        assert(selectDeterministicCandidates(
            deterministicCandidates, 4u, 1u) == expectedBudgetFour);
        assert(selectDeterministicCandidates(
            deterministicCandidates, 1u, 1u) ==
            std::vector<uint32_t>{
                GetSvsmDeterministicFinePageOrderKey(
                    0u, centerPage)
            });
        // Once new residency is exhausted, missing pages do not issue more
        // reservations, while later already-resident dirty pages can still
        // fill the finite render budget.
        const std::vector<uint32_t> expectedPoolPressure = {
            GetSvsmDeterministicFinePageOrderKey(
                0u, centerPage + 1u),
            GetSvsmDeterministicFinePageOrderKey(1u, centerPage),
            GetSvsmDeterministicFinePageOrderKey(4u, centerPage)
        };
        assert(selectDeterministicCandidates(
            deterministicCandidates, 4u, 0u) == expectedPoolPressure);
        assert(selectDeterministicBitmaskCandidates(
            deterministicCandidates, 4u, 0u) ==
            expectedPoolPressure);
        std::rotate(
            deterministicCandidates.begin(),
            deterministicCandidates.begin() + 3,
            deterministicCandidates.end());
        assert(selectDeterministicCandidates(
            deterministicCandidates, 4u, 0u) == expectedPoolPressure);
        // Repeating a moving-light invalidation with identical eligibility
        // produces the same ordered fine winners every time.
        const auto repeatedMovingLightSelection =
            selectDeterministicCandidates(
                deterministicCandidates, 4u, 0u);
        assert(selectDeterministicCandidates(
            deterministicCandidates, 4u, 0u) ==
            repeatedMovingLightSelection);

        const std::array<uint32_t, 8u> maskBoundaryOrders = {
            0u, 31u, 32u, 63u, 64u, 2047u, 2048u, 4095u
        };
        const auto wrapPage = [](int32_t coordinate) {
            const int32_t pageCount = int32_t(SvsmPagesPerAxis);
            const int32_t remainder = coordinate % pageCount;
            return uint32_t(
                remainder < 0
                    ? remainder + pageCount
                    : remainder);
        };
        const std::array<std::array<int32_t, 2u>,
            SvsmFineClipmapCount> candidateMaskOffsets = {{
                {{ 0, 0 }},
                {{ 1, -1 }},
                {{ -63, 63 }},
                {{ 64, -64 }},
                {{ 129, -130 }}
            }};
        for (uint32_t level = 0u;
            level < SvsmFineClipmapCount;
            ++level)
        {
            for (uint32_t order : maskBoundaryOrders)
            {
                const uint32_t localPage =
                    deterministicPageForOrder[order];
                const uint32_t localX =
                    localPage % SvsmPagesPerAxis;
                const uint32_t localY =
                    localPage / SvsmPagesPerAxis;
                const uint32_t tableX = wrapPage(
                    int32_t(localX) +
                    candidateMaskOffsets[level][0]);
                const uint32_t tableY = wrapPage(
                    int32_t(localY) +
                    candidateMaskOffsets[level][1]);
                const uint32_t recoveredLocalX = wrapPage(
                    int32_t(tableX) -
                    candidateMaskOffsets[level][0]);
                const uint32_t recoveredLocalY = wrapPage(
                    int32_t(tableY) -
                    candidateMaskOffsets[level][1]);
                const uint32_t recoveredLocalPage =
                    recoveredLocalY * SvsmPagesPerAxis +
                    recoveredLocalX;
                const uint32_t globalOrder =
                    GetSvsmDeterministicFinePageOrderKey(
                        level, recoveredLocalPage);
                assert(globalOrder ==
                    level * SvsmPagesPerClipmap + order);
                const uint32_t maskWord = globalOrder / 32u;
                const uint32_t maskBit =
                    1u << (globalOrder & 31u);
                assert(maskWord <
                    SvsmFinePageCandidateMaskWordCount);
                assert(maskWord ==
                    level *
                        SvsmFinePageCandidateMaskWordsPerLevel +
                    order / 32u);
                assert(maskBit == (1u << (order & 31u)));
            }
        }
        std::vector<DeterministicBudgetCandidate>
            maskBoundaryCandidates;
        for (uint32_t level = 0u;
            level < SvsmClipmapCount - 1u;
            ++level)
        {
            for (uint32_t order : maskBoundaryOrders)
            {
                maskBoundaryCandidates.push_back({
                    level,
                    deterministicPageForOrder[order],
                    true,
                    (order & 1u) != 0u,
                    (order & 1u) != 0u
                });
            }
        }
        const std::array<uint32_t, 10u> maskBudgets = {
            0u, 1u, 4u, 31u, 32u, 63u, 64u, 65u, 1024u, 4095u
        };
        for (uint32_t budget : maskBudgets)
        {
            for (uint32_t available : {
                    0u,
                    1u,
                    budget > 0u ? budget - 1u : 0u,
                    budget })
            {
                assert(selectDeterministicBitmaskCandidates(
                    maskBoundaryCandidates,
                    budget,
                    available) ==
                    selectDeterministicCandidates(
                        maskBoundaryCandidates,
                        budget,
                        available));
            }
        }

        std::vector<DeterministicBudgetCandidate>
            randomizedMaskCandidates;
        randomizedMaskCandidates.reserve(1000u);
        uint32_t deterministicMaskRandomState = 0x243f6a88u;
        for (uint32_t index = 0u; index < 1000u; ++index)
        {
            deterministicMaskRandomState =
                deterministicMaskRandomState * 1664525u +
                1013904223u;
            const uint32_t level =
                index % (SvsmClipmapCount - 1u);
            const uint32_t order =
                (index * 4051u) % SvsmPagesPerClipmap;
            const bool required =
                (deterministicMaskRandomState & 1u) != 0u;
            const bool resident =
                (deterministicMaskRandomState & 2u) != 0u;
            const bool dirty =
                (deterministicMaskRandomState & 4u) != 0u;
            randomizedMaskCandidates.push_back({
                level,
                deterministicPageForOrder[order],
                required,
                dirty,
                resident
            });
        }
        for (uint32_t budget : maskBudgets)
        {
            for (uint32_t available : {
                    0u,
                    1u,
                    budget > 0u ? budget - 1u : 0u,
                    budget })
            {
                assert(selectDeterministicBitmaskCandidates(
                    randomizedMaskCandidates,
                    budget,
                    available) ==
                    selectDeterministicCandidates(
                        randomizedMaskCandidates,
                        budget,
                        available));
            }
        }

        struct DeterministicRequiredFineVictim
        {
            uint32_t level;
            uint32_t page;
            uint32_t physical;
            bool required;
            bool resident;
        };
        struct DeterministicRequiredFineVictimSelection
        {
            std::vector<uint32_t> keys;
            std::vector<uint32_t> physicalPages;
        };
        const auto selectDeterministicRequiredFineVictims =
            [](const std::vector<
                    DeterministicRequiredFineVictim>& candidates,
                uint32_t maximumVictims)
            {
                std::array<
                    uint32_t,
                    SvsmFinePageCandidateMaskWordCount> masks{};
                std::vector<uint32_t> physicalForKey(
                    SvsmFineClipmapCount *
                        SvsmPagesPerClipmap,
                    SvsmInvalidPhysicalPage);
                uint32_t victimCount = 0u;
                for (const auto& candidate : candidates)
                {
                    if (!candidate.required ||
                        !candidate.resident ||
                        candidate.level >=
                            SvsmFineClipmapCount ||
                        candidate.page >=
                            SvsmPagesPerClipmap)
                    {
                        continue;
                    }
                    const uint32_t key =
                        GetSvsmDeterministicFinePageOrderKey(
                            candidate.level,
                            candidate.page);
                    const uint32_t word = key / 32u;
                    const uint32_t bit =
                        1u << (key & 31u);
                    masks[word] |= bit;
                    physicalForKey[key] =
                        candidate.physical;
                    ++victimCount;
                }

                DeterministicRequiredFineVictimSelection
                    selection;
                const uint32_t selectionCount =
                    std::min(maximumVictims, victimCount);
                for (uint32_t victimRank = 0u;
                    victimRank < selectionCount;
                    ++victimRank)
                {
                    uint32_t remainingRank = victimRank;
                    for (int32_t word =
                            int32_t(
                                SvsmFinePageCandidateMaskWordCount) -
                            1;
                        word >= 0;
                        --word)
                    {
                        const uint32_t candidatesInWord =
                            masks[uint32_t(word)];
                        uint32_t bitsToCount =
                            candidatesInWord;
                        uint32_t wordCount = 0u;
                        while (bitsToCount != 0u)
                        {
                            bitsToCount &=
                                bitsToCount - 1u;
                            ++wordCount;
                        }
                        if (remainingRank >= wordCount)
                        {
                            remainingRank -= wordCount;
                            continue;
                        }

                        uint32_t remainingCandidates =
                            candidatesInWord;
                        while (remainingRank > 0u)
                        {
                            uint32_t skippedBit = 31u;
                            while ((remainingCandidates &
                                    (1u << skippedBit)) ==
                                0u)
                            {
                                --skippedBit;
                            }
                            remainingCandidates &=
                                ~(1u << skippedBit);
                            --remainingRank;
                        }

                        uint32_t bitIndex = 31u;
                        while ((remainingCandidates &
                                (1u << bitIndex)) == 0u)
                        {
                            --bitIndex;
                        }
                        const uint32_t key =
                            uint32_t(word) * 32u +
                            bitIndex;
                        const uint32_t physical =
                            physicalForKey[key];
                        if (physical !=
                            SvsmInvalidPhysicalPage)
                        {
                            selection.keys.push_back(key);
                            selection.physicalPages.push_back(
                                physical);
                        }
                        break;
                    }
                }
                return selection;
            };
        const auto requiredFineVictimCountForCoarsePressure =
            [](uint32_t missingCoarsePages,
                uint32_t freePages,
                uint32_t unrecentPages,
                uint32_t recentPages)
            {
                const uint64_t nonrequiredCapacity =
                    uint64_t(freePages) +
                    uint64_t(unrecentPages) +
                    uint64_t(recentPages);
                return nonrequiredCapacity >=
                        missingCoarsePages
                    ? 0u
                    : uint32_t(
                        uint64_t(missingCoarsePages) -
                        nonrequiredCapacity);
            };

        // Coarse fallback consumes free and nonrequired cached pages first.
        // Any remaining pressure evicts the worst required-fine virtual
        // priorities, independent of physical recycle append order.
        assert(requiredFineVictimCountForCoarsePressure(
            5u, 1u, 1u, 1u) == 2u);
        std::vector<DeterministicRequiredFineVictim>
            requiredFineVictims = {
                { 0u, deterministicPageForOrder[0u],
                    7u, true, true },
                { 0u, deterministicPageForOrder[4095u],
                    8u, true, true },
                { 1u, deterministicPageForOrder[0u],
                    9u, true, true },
                { 4u, deterministicPageForOrder[0u],
                    10u, true, true },
                { 4u, deterministicPageForOrder[4095u],
                    11u, true, true },
                { coarsestLevel,
                    deterministicPageForOrder[4095u],
                    12u, true, true },
                { 3u, deterministicPageForOrder[4095u],
                    13u, false, true },
                { 2u, deterministicPageForOrder[4095u],
                    14u, true, false }
            };
        const std::vector<uint32_t> expectedVictimKeys = {
            GetSvsmDeterministicFinePageOrderKey(
                4u, deterministicPageForOrder[4095u]),
            GetSvsmDeterministicFinePageOrderKey(
                4u, deterministicPageForOrder[0u])
        };
        const std::vector<uint32_t>
            expectedVictimPhysicalPages = { 11u, 10u };
        const auto coarsePressureVictims =
            selectDeterministicRequiredFineVictims(
                requiredFineVictims,
                requiredFineVictimCountForCoarsePressure(
                    5u, 1u, 1u, 1u));
        assert(coarsePressureVictims.keys ==
            expectedVictimKeys);
        assert(coarsePressureVictims.physicalPages ==
            expectedVictimPhysicalPages);

        std::reverse(
            requiredFineVictims.begin(),
            requiredFineVictims.end());
        const auto reversedCoarsePressureVictims =
            selectDeterministicRequiredFineVictims(
                requiredFineVictims, 2u);
        assert(reversedCoarsePressureVictims.keys ==
            expectedVictimKeys);
        assert(
            reversedCoarsePressureVictims.physicalPages ==
            expectedVictimPhysicalPages);
        for (uint32_t repeat = 0u;
            repeat < requiredFineVictims.size();
            ++repeat)
        {
            std::rotate(
                requiredFineVictims.begin(),
                requiredFineVictims.begin() + 1u,
                requiredFineVictims.end());
            const auto repeated =
                selectDeterministicRequiredFineVictims(
                    requiredFineVictims, 2u);
            assert(repeated.keys == expectedVictimKeys);
            assert(repeated.physicalPages ==
                expectedVictimPhysicalPages);
        }

        // Exhaustion returns every valid required-fine victim exactly once
        // and then terminates. Coarsest and invalid/nonresident entries never
        // enter the mask.
        const auto exhaustedVictims =
            selectDeterministicRequiredFineVictims(
                requiredFineVictims,
                std::numeric_limits<uint32_t>::max());
        assert(exhaustedVictims.keys.size() == 5u);
        assert(exhaustedVictims.physicalPages.size() ==
            exhaustedVictims.keys.size());
        assert(std::adjacent_find(
            exhaustedVictims.keys.begin(),
            exhaustedVictims.keys.end()) ==
            exhaustedVictims.keys.end());

        // Model the post-coarse state with no capacity left for new fine
        // residency. A newly missing highest-priority page and deterministic
        // coarse victims are skipped, while later resident-dirty pages still
        // fill as much of the fine render budget as possible.
        std::vector<DeterministicBudgetCandidate>
            postCoarseFineCandidates;
        for (const auto& candidate : requiredFineVictims)
        {
            if (candidate.level >=
                    SvsmFineClipmapCount ||
                !candidate.required ||
                !candidate.resident)
            {
                continue;
            }
            const uint32_t key =
                GetSvsmDeterministicFinePageOrderKey(
                    candidate.level,
                    candidate.page);
            const bool coarseVictim =
                std::find(
                    expectedVictimKeys.begin(),
                    expectedVictimKeys.end(),
                    key) != expectedVictimKeys.end();
            const bool newlyMissingCenter =
                key ==
                    GetSvsmDeterministicFinePageOrderKey(
                        0u,
                        deterministicPageForOrder[0u]);
            postCoarseFineCandidates.push_back({
                candidate.level,
                candidate.page,
                true,
                true,
                !coarseVictim &&
                    !newlyMissingCenter
            });
        }
        const std::vector<uint32_t>
            expectedResidentDirtyContinuation = {
                GetSvsmDeterministicFinePageOrderKey(
                    0u,
                    deterministicPageForOrder[4095u]),
                GetSvsmDeterministicFinePageOrderKey(
                    1u,
                    deterministicPageForOrder[0u])
            };
        assert(selectDeterministicCandidates(
            postCoarseFineCandidates, 4u, 0u) ==
            expectedResidentDirtyContinuation);
        std::reverse(
            postCoarseFineCandidates.begin(),
            postCoarseFineCandidates.end());
        assert(selectDeterministicCandidates(
            postCoarseFineCandidates, 4u, 0u) ==
            expectedResidentDirtyContinuation);

        // The independent all-level mode shares one reservation with the
        // coarsest clipmap, providing a hard per-frame workload bound.
        uint32_t allLevelReservation = 0u;
        const auto scheduleAllLevels =
            [&allLevelReservation](uint32_t clipmapLevel, uint32_t budget)
            {
                const uint32_t reservation = allLevelReservation;
                if (clipmapLevel < SvsmClipmapCount)
                    ++allLevelReservation;
                return ShouldScheduleSvsmDirtyPageRender(
                    clipmapLevel, reservation, budget, true);
            };
        assert(scheduleAllLevels(coarsestLevel, 2u));
        assert(scheduleAllLevels(4u, 2u));
        assert(!scheduleAllLevels(coarsestLevel, 2u));
        assert(allLevelReservation == 3u);
        allLevelReservation = 0u;
        assert(!scheduleAllLevels(coarsestLevel, 0u));
        assert(allLevelReservation == 1u);

        // A zero budget rejects fine work without consuming coarse capacity.
        fineReservation = 0u;
        assert(!schedule(3u, 0u));
        assert(fineReservation == 1u);
        assert(schedule(coarsestLevel, 0u));
        assert(fineReservation == 1u);

        // Resetting the per-frame reservation lets a finite budget drain all
        // pending fine pages over subsequent frames.
        uint32_t remainingFinePages = 5u;
        constexpr std::array<uint32_t, 3> expectedScheduled = {
            2u, 2u, 1u
        };
        for (uint32_t expected : expectedScheduled)
        {
            fineReservation = 0u;
            uint32_t scheduled = 0u;
            for (uint32_t page = 0u;
                page < remainingFinePages;
                ++page)
            {
                const uint32_t level = page % coarsestLevel;
                scheduled += schedule(level, 2u) ? 1u : 0u;
            }
            assert(scheduled == expected);
            remainingFinePages -= scheduled;
        }
        assert(remainingFinePages == 0u);

        // UINT_MAX admits a full reachable shared physical pool of fine work.
        fineReservation = 0u;
        for (uint32_t page = 0u;
            page < SvsmPagesPerClipmap;
            ++page)
        {
            assert(schedule(page % coarsestLevel, unlimited));
        }
        assert(fineReservation == SvsmPagesPerClipmap);

        // Invalid clipmap levels neither schedule nor consume a reservation.
        const uint32_t reservationBeforeInvalid = fineReservation;
        assert(!ShouldScheduleSvsmDirtyPageRender(
            SvsmClipmapCount, 0u, unlimited));
        assert(!schedule(SvsmClipmapCount, unlimited));
        assert(fineReservation == reservationBeforeInvalid);

        // The optional relaxed probe is active only for finite budgets. It
        // can skip a fine reservation only after the monotonic counter has
        // reached that budget; coarsest work always bypasses it.
        assert(!ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
            false, 4u));
        assert(ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
            true, 4u));
        assert(ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
            true, 0u));
        assert(!ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
            true, unlimited));
        assert(!IsSvsmAllocationBudgetSaturationEarlyOutActive(
            SvsmMode::DenseReference, true, 4u));
        assert(IsSvsmAllocationBudgetSaturationEarlyOutActive(
            SvsmMode::SparseUncached, true, 4u));
        assert(IsSvsmAllocationBudgetSaturationEarlyOutActive(
            SvsmMode::SparseCached, true, 4u));
        assert(!IsSvsmAllocationBudgetSaturationEarlyOutActive(
            SvsmMode::SparseCached, true, unlimited));
        assert(!ShouldSkipSvsmFineRenderReservationAtomic(
            0u, 3u, 4u, true));
        assert(ShouldSkipSvsmFineRenderReservationAtomic(
            0u, 4u, 4u, true));
        assert(ShouldSkipSvsmFineRenderReservationAtomic(
            4u, 9u, 4u, true));
        assert(!ShouldSkipSvsmFineRenderReservationAtomic(
            coarsestLevel, 4u, 4u, true));
        assert(ShouldSkipSvsmFineRenderReservationAtomic(
            coarsestLevel, 4u, 4u, true, true));
        assert(!ShouldSkipSvsmFineRenderReservationAtomic(
            0u, 4u, 4u, false));
        assert(!ShouldSkipSvsmFineRenderReservationAtomic(
            SvsmClipmapCount, 4u, 4u, true, true));
    }

    void TestLightDepthOriginGuardBand()
    {
        SvsmProjectedDepthInterval interval;
        assert(TryBuildSvsmProjectedDepthInterval(
            10.f, 5.f, false, 0.f, interval));
        assert(interval.minimum == 5.f);
        assert(interval.maximum == 15.f);

        assert(TryBuildSvsmProjectedDepthInterval(
            10.f, 5.f, true, -2.f, interval));
        assert(interval.minimum == -2.f);
        assert(interval.maximum == 15.f);
        assert(TryBuildSvsmProjectedDepthInterval(
            20.f, 60.f, true, -80.f, interval));
        assert(interval.minimum == -80.f);
        assert(interval.maximum == 80.f);
        assert(TryBuildSvsmProjectedAabbDepthInterval(
            10.f,
            { -2.f, -4.f, -6.f },
            { 2.f, 4.f, 6.f },
            { 0.5f, -0.25f, 1.f },
            true,
            0.f,
            interval));
        assert(interval.minimum == 0.f);
        assert(interval.maximum == 18.f);
        assert(!TryBuildSvsmProjectedAabbDepthInterval(
            0.f,
            { 1.f, 0.f, 0.f },
            { -1.f, 1.f, 1.f },
            { 1.f, 0.f, 0.f },
            false,
            0.f,
            interval));
        assert(!TryBuildSvsmProjectedAabbDepthInterval(
            0.f,
            {
                -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max()
            },
            {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()
            },
            {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()
            },
            false,
            0.f,
            interval));
        assert(!TryBuildSvsmProjectedDepthInterval(
            0.f, -1.f, false, 0.f, interval));
        assert(!TryBuildSvsmProjectedDepthInterval(
            std::numeric_limits<float>::infinity(),
            1.f,
            false,
            0.f,
            interval));
        assert(!TryBuildSvsmProjectedDepthInterval(
            0.f,
            std::numeric_limits<float>::infinity(),
            false,
            0.f,
            interval));
        assert(!TryBuildSvsmProjectedDepthInterval(
            0.f,
            1.f,
            true,
            std::numeric_limits<float>::quiet_NaN(),
            interval));

        auto select = [](
            bool enabled,
            bool sparseCached,
            bool cacheValid,
            bool committedValid,
            bool sameLight,
            bool sameBasis,
            bool sameDepth,
            SvsmProjectedDepthInterval projectedInterval,
            bool intervalValid = true) {
            return SelectSvsmLightDepthOrigin(
                enabled,
                sparseCached,
                cacheValid,
                committedValid,
                sameLight,
                sameBasis,
                sameDepth,
                17.f,
                0.f,
                200.f,
                0.9f,
                intervalValid,
                projectedInterval);
        };
        const SvsmProjectedDepthInterval exactBoundary = {
            -90.f, 90.f
        };
        const SvsmLightDepthOriginDecision retained = select(
            true, true, true, true, true, true, true, exactBoundary);
        assert(retained.valid);
        assert(retained.retainedCommittedOrigin);
        assert(retained.selectedOrigin == 0.f);

        const float outsideMinimum = std::nextafter(
            -90.f, -std::numeric_limits<float>::infinity());
        const float outsideMaximum = std::nextafter(
            90.f, std::numeric_limits<float>::infinity());
        assert(!select(
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            { outsideMinimum, 90.f }).retainedCommittedOrigin);
        assert(!select(
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            { -90.f, outsideMaximum }).retainedCommittedOrigin);

        auto assertLegacyOrigin = [&select](
            bool enabled,
            bool sparseCached,
            bool cacheValid,
            bool committedValid,
            bool sameLight,
            bool sameBasis,
            bool sameDepth) {
            const SvsmLightDepthOriginDecision decision = select(
                enabled,
                sparseCached,
                cacheValid,
                committedValid,
                sameLight,
                sameBasis,
                sameDepth,
                { -1.f, 1.f });
            assert(decision.valid);
            assert(!decision.retainedCommittedOrigin);
            assert(decision.selectedOrigin == 17.f);
        };
        assertLegacyOrigin(
            false, true, true, true, true, true, true);
        assertLegacyOrigin(
            true, false, true, true, true, true, true);
        assertLegacyOrigin(
            true, true, false, true, true, true, true);
        assertLegacyOrigin(
            true, true, true, false, true, true, true);
        assertLegacyOrigin(
            true, true, true, true, false, true, true);
        assertLegacyOrigin(
            true, true, true, true, true, false, true);
        assertLegacyOrigin(
            true, true, true, true, true, true, false);

        assert(!select(
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            { -1.f, 1.f },
            false).retainedCommittedOrigin);
        assert(!select(
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            { -100.f, 100.f }).retainedCommittedOrigin);
        assert(!select(
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            {
                -1.f,
                std::numeric_limits<float>::infinity()
            }).retainedCommittedOrigin);
        assert(!select(
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            { 1.f, -1.f }).retainedCommittedOrigin);

        assert(!SelectSvsmLightDepthOrigin(
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            17.f,
            0.f,
            std::numeric_limits<float>::infinity(),
            0.9f,
            true,
            { -1.f, 1.f }).retainedCommittedOrigin);
        assert(!SelectSvsmLightDepthOrigin(
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            17.f,
            0.f,
            200.f,
            0.f,
            true,
            { -1.f, 1.f }).retainedCommittedOrigin);
        assert(!SelectSvsmLightDepthOrigin(
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            17.f,
            0.f,
            200.f,
            std::nextafter(
                1.f,
                std::numeric_limits<float>::infinity()),
            true,
            { -1.f, 1.f }).retainedCommittedOrigin);
        const SvsmLightDepthOriginDecision invalidRequested =
            SelectSvsmLightDepthOrigin(
                true,
                true,
                true,
                true,
                true,
                true,
                true,
                std::numeric_limits<float>::quiet_NaN(),
                0.f,
                200.f,
                0.9f,
                true,
                { -1.f, 1.f });
        assert(!invalidRequested.valid);
        assert(!invalidRequested.retainedCommittedOrigin);

        assert(GetNextSvsmCommittedLightDepthOrigin(
            3.f, 17.f, false) == 3.f);
        assert(GetNextSvsmCommittedLightDepthOrigin(
            3.f, 17.f, true) == 17.f);
    }

    void TestMappingAndWraparound()
    {
        assert(WrapSvsmPageCoordinate(-1) == 63);
        assert(WrapSvsmPageCoordinate(-65) == 63);
        assert(WrapSvsmPageCoordinate(64) == 0);
        assert(WrapSvsmPageCoordinate(129) == 1);
        assert(WrapSvsmPageCoordinate(
            std::numeric_limits<int32_t>::min()) == 0);
        assert(WrapSvsmPageCoordinate(
            std::numeric_limits<int32_t>::max()) == 63);

        int32_t quantizedOrigin = 0;
        assert(TryQuantizeSvsmRenderOrigin(
            64.f, 2.f, quantizedOrigin));
        assert(quantizedOrigin == 32);
        assert(!TryQuantizeSvsmRenderOrigin(
            1.f, 0.f, quantizedOrigin));
        assert(!TryQuantizeSvsmRenderOrigin(
            1.f, -1.f, quantizedOrigin));
        assert(!TryQuantizeSvsmRenderOrigin(
            std::numeric_limits<float>::quiet_NaN(),
            1.f,
            quantizedOrigin));
        assert(!TryQuantizeSvsmRenderOrigin(
            std::numeric_limits<float>::infinity(),
            1.f,
            quantizedOrigin));
        assert(!TryQuantizeSvsmRenderOrigin(
            std::numeric_limits<float>::max(),
            1.f,
            quantizedOrigin));
        const float maximumSafeOrigin = std::nextafter(
            float(std::numeric_limits<int32_t>::max()),
            0.f);
        assert(TryQuantizeSvsmRenderOrigin(
            maximumSafeOrigin, 1.f, quantizedOrigin));
        assert(quantizedOrigin == int32_t(maximumSafeOrigin));
        assert(TryQuantizeSvsmRenderOrigin(
            float(std::numeric_limits<int32_t>::min()),
            1.f,
            quantizedOrigin));
        assert(quantizedOrigin ==
            std::numeric_limits<int32_t>::min());

        const SvsmPageCoordinate extremeOffset =
            SvsmPageTableOffsetForRenderOrigin({
                std::numeric_limits<int32_t>::min(),
                std::numeric_limits<int32_t>::max()
            });
        assert(extremeOffset.x >= 0 &&
            extremeOffset.x < int32_t(SvsmPagesPerAxis));
        assert(extremeOffset.y >= 0 &&
            extremeOffset.y < int32_t(SvsmPagesPerAxis));
        assert((SvsmPageTableDeltaForRenderOrigins(
            {
                std::numeric_limits<int32_t>::max(),
                std::numeric_limits<int32_t>::max()
            },
            {
                std::numeric_limits<int32_t>::min(),
                std::numeric_limits<int32_t>::min()
            }) == SvsmPageCoordinate{ 64, -64 }));
        assert((SvsmPageTableDeltaForRenderOrigins(
            {
                std::numeric_limits<int32_t>::min(),
                std::numeric_limits<int32_t>::min()
            },
            {
                std::numeric_limits<int32_t>::max(),
                std::numeric_limits<int32_t>::max()
            }) == SvsmPageCoordinate{ -64, 64 }));
        assert(IsSvsmTablePageNewlyExposed(
            { 0, 0 }, { 0, 0 }, { 64, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            { 0, 0 }, { 0, 0 }, { 0, -64 }));
        assert(IsSvsmProjectionRangeRepresentable(20.f, 200.f));
        assert(!IsSvsmProjectionRangeRepresentable(
            std::numeric_limits<float>::denorm_min(), 200.f));
        assert(!IsSvsmProjectionRangeRepresentable(
            20.f, std::numeric_limits<float>::denorm_min()));
        SparseVirtualShadowMapSettings extremeExtentSettings;
        extremeExtentSettings.firstClipmapExtent =
            std::numeric_limits<float>::max();
        assert(!ValidateSvsmSettings(extremeExtentSettings));

        SvsmPageCoordinate offset{ 63, 1 };
        offset = AdvanceSvsmWrapOffset(offset, { 2, -3 });
        assert((offset == SvsmPageCoordinate{ 1, 62 }));

        assert((SvsmPageTableOffsetForRenderOrigin({ 0, 0 }) ==
            SvsmPageCoordinate{ 32, 32 }));
        assert((SvsmPageTableOffsetForRenderOrigin({ 3, 5 }) ==
            SvsmPageCoordinate{ 35, 27 }));
        assert((SvsmPageTableDeltaForRenderOrigins(
            { 3, 5 }, { 2, 3 }) ==
            SvsmPageCoordinate{ 1, -2 }));

        // The D3D virtual-texture conversion flips NDC Y. Moving a render
        // origin +1 page therefore moves a fixed world's local virtual Y +1
        // page, while the table offset must move -1 to keep its table address
        // stable. X has the opposite local direction and keeps a + delta.
        const SvsmPageCoordinate oldOffset =
            SvsmPageTableOffsetForRenderOrigin({ 2, 3 });
        const SvsmPageCoordinate newOffset =
            SvsmPageTableOffsetForRenderOrigin({ 3, 5 });
        const SvsmPageCoordinate fixedWorldOldLocal{ 30, 35 };
        const SvsmPageCoordinate fixedWorldNewLocal{ 29, 37 };
        assert((WrapSvsmPageCoordinate({
            fixedWorldOldLocal.x + oldOffset.x,
            fixedWorldOldLocal.y + oldOffset.y
        }) == WrapSvsmPageCoordinate({
            fixedWorldNewLocal.x + newOffset.x,
            fixedWorldNewLocal.y + newOffset.y
        })));

        const SvsmPageCoordinate currentOffset{ 35, 27 };
        auto tableForLocal = [currentOffset](
            SvsmPageCoordinate localPage) {
            return WrapSvsmPageCoordinate({
                localPage.x + currentOffset.x,
                localPage.y + currentOffset.y
            });
        };
        assert(!IsSvsmTablePageNewlyExposed(
            tableForLocal({ 63, 63 }), currentOffset, { 0, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 63, 20 }), currentOffset, { 1, 0 }));
        assert(!IsSvsmTablePageNewlyExposed(
            tableForLocal({ 62, 20 }), currentOffset, { 1, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 0, 20 }), currentOffset, { -1, 0 }));
        assert(!IsSvsmTablePageNewlyExposed(
            tableForLocal({ 1, 20 }), currentOffset, { -1, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 20, 63 }), currentOffset, { 0, 1 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 20, 0 }), currentOffset, { 0, -1 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 1, 63 }), currentOffset, { 63, 1 }));
        assert(!IsSvsmTablePageNewlyExposed(
            tableForLocal({ 0, 62 }), currentOffset, { 63, 1 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 62, 0 }), currentOffset, { -63, -1 }));
        assert(!IsSvsmTablePageNewlyExposed(
            tableForLocal({ 63, 1 }), currentOffset, { -63, -1 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 32, 32 }), currentOffset, { 64, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 32, 32 }), currentOffset, { -64, 0 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 32, 32 }), currentOffset, { 0, 64 }));
        assert(IsSvsmTablePageNewlyExposed(
            tableForLocal({ 32, 32 }), currentOffset, { 0, -64 }));

        assert(SnapSvsmRenderOrigin(3.99f, 2.f) == 2.f);
        assert(SnapSvsmRenderOrigin(-0.01f, 2.f) == -2.f);
        assert(SnapSvsmRenderOrigin(4.f, 2.f) == 4.f);
    }

    void TestPerPixelRequestDeduplication()
    {
        constexpr uint32_t HashSize = 64u;
        constexpr uint32_t ProbeCount = 4u;
        constexpr uint32_t InvalidOwner =
            std::numeric_limits<uint32_t>::max();
        auto hashValue = [](uint32_t owner) {
            uint32_t hash = owner;
            hash ^= hash >> 8u;
            hash *= 0x9e3779b1u;
            hash ^= hash >> 16u;
            return hash;
        };
        auto hashSlot = [&](uint32_t owner) {
            return hashValue(owner) & (HashSize - 1u);
        };
        auto hashStep = [&](uint32_t owner) {
            return ((hashValue(owner) >> 6u) | 1u) &
                (HashSize - 1u);
        };
        auto probeSlot = [&](uint32_t owner, uint32_t probe) {
            return (hashSlot(owner) + probe * hashStep(owner)) &
                (HashSize - 1u);
        };

        std::array<std::vector<uint32_t>, HashSize * HashSize>
            ownersByProbeSequence;
        for (uint32_t owner = 0u;
            owner < SvsmClipmapCount * SvsmPagesPerClipmap;
            ++owner)
        {
            const uint32_t step = hashStep(owner);
            assert(step > 0u);
            assert((step & 1u) != 0u);
            std::array<bool, HashSize> visited = {};
            for (uint32_t probe = 0u;
                probe < ProbeCount;
                ++probe)
            {
                const uint32_t slot = probeSlot(owner, probe);
                assert(!visited[slot]);
                visited[slot] = true;
            }

            ownersByProbeSequence[
                hashSlot(owner) * HashSize + step].push_back(owner);
        }
        const auto forcedSequence = std::find_if(
            ownersByProbeSequence.begin(),
            ownersByProbeSequence.end(),
            [=](const std::vector<uint32_t>& owners) {
                return owners.size() > ProbeCount;
            });
        assert(forcedSequence != ownersByProbeSequence.end());
        for (uint32_t owner : *forcedSequence)
        {
            assert(hashSlot(owner) ==
                hashSlot(forcedSequence->front()));
            assert(hashStep(owner) ==
                hashStep(forcedSequence->front()));
        }

        const uint32_t centerOwner = EncodeSvsmVirtualPageOwner(
            { 12, 19 }, 0u);
        const uint32_t neighborOwner = EncodeSvsmVirtualPageOwner(
            { 13, 19 }, 0u);
        const uint32_t coarseOwner = EncodeSvsmVirtualPageOwner(
            { 31, 31 }, SvsmClipmapCount - 1u);
        const std::vector<uint32_t> requests = {
            centerOwner,
            centerOwner,
            neighborOwner,
            coarseOwner,
            coarseOwner,
            (*forcedSequence)[0],
            (*forcedSequence)[0],
            (*forcedSequence)[1],
            (*forcedSequence)[2],
            (*forcedSequence)[3],
            (*forcedSequence)[4]
        };

        struct DeduplicationResult
        {
            std::vector<uint32_t> publishedOwners;
            uint32_t directGlobalWrites = 0u;
            uint32_t localCompareExchangeAttempts = 0u;
            uint32_t waveSuppressedRequests = 0u;
            bool sameOwnerSuppressed = false;
            bool collisionOpenAddressed = false;
            bool probeLimitFailedOpen = false;
        };
        auto simulateRequests =
            [&](const std::vector<uint32_t>& source,
                bool localDedupeEnabled,
                uint32_t waveSize = 1u)
            {
                DeduplicationResult result;
                std::array<uint32_t, 64u> localHash;
                localHash.fill(InvalidOwner);
                assert(waveSize > 0u && waveSize <= 128u);
                for (std::size_t requestIndex = 0u;
                    requestIndex < source.size();
                    ++requestIndex)
                {
                    const uint32_t owner = source[requestIndex];
                    if (!localDedupeEnabled)
                    {
                        result.publishedOwners.push_back(owner);
                        ++result.directGlobalWrites;
                        continue;
                    }

                    // WaveMatch elects the highest active matching lane. A
                    // duplicate in another wave remains an independent
                    // request and is still suppressed safely by the group
                    // hash.
                    const std::size_t waveEnd = std::min(
                        source.size(),
                        ((requestIndex / waveSize) + 1u) * waveSize);
                    bool highestMatchingLane = true;
                    for (std::size_t later = requestIndex + 1u;
                        later < waveEnd;
                        ++later)
                    {
                        if (source[later] == owner)
                        {
                            highestMatchingLane = false;
                            break;
                        }
                    }
                    if (!highestMatchingLane)
                    {
                        ++result.waveSuppressedRequests;
                        continue;
                    }

                    bool locallyRecorded = false;
                    for (uint32_t probe = 0u;
                        probe < ProbeCount;
                        ++probe)
                    {
                        ++result.localCompareExchangeAttempts;
                        const uint32_t slot =
                            probeSlot(owner, probe);
                        const uint32_t previous = localHash[slot];
                        if (previous == InvalidOwner)
                        {
                            localHash[slot] = owner;
                            result.collisionOpenAddressed |=
                                probe > 0u;
                            locallyRecorded = true;
                            break;
                        }
                        if (previous == owner)
                        {
                            result.sameOwnerSuppressed = true;
                            result.collisionOpenAddressed |=
                                probe > 0u;
                            locallyRecorded = true;
                            break;
                        }
                    }
                    if (!locallyRecorded)
                    {
                        result.publishedOwners.push_back(owner);
                        ++result.directGlobalWrites;
                        result.probeLimitFailedOpen = true;
                    }
                }
                for (uint32_t owner : localHash)
                {
                    if (owner != InvalidOwner)
                        result.publishedOwners.push_back(owner);
                }
                return result;
            };

        std::vector<uint32_t> referenceOwners = requests;
        auto sortAndDeduplicate = [](std::vector<uint32_t>& owners) {
            std::sort(owners.begin(), owners.end());
            owners.erase(
                std::unique(owners.begin(), owners.end()),
                owners.end());
        };
        sortAndDeduplicate(referenceOwners);

        DeduplicationResult deduplicated =
            simulateRequests(requests, true);
        sortAndDeduplicate(deduplicated.publishedOwners);
        assert(deduplicated.sameOwnerSuppressed);
        assert(deduplicated.collisionOpenAddressed);
        assert(deduplicated.probeLimitFailedOpen);
        assert(deduplicated.directGlobalWrites > 0u);
        assert(deduplicated.publishedOwners == referenceOwners);

        DeduplicationResult reference =
            simulateRequests(requests, false, 64u);
        assert(reference.directGlobalWrites == requests.size());
        assert(reference.waveSuppressedRequests == 0u);
        sortAndDeduplicate(reference.publishedOwners);
        assert(reference.publishedOwners == referenceOwners);

        // Coherent receiver waves collapse to one shared-memory operation per
        // owner. Wave boundaries are intentionally independent, matching the
        // active-lane scope of WaveMatch on both wave32 and wave64 hardware.
        std::vector<uint32_t> coherentRequests(64u, centerOwner);
        const DeduplicationResult coherentWithoutWave =
            simulateRequests(coherentRequests, true);
        const DeduplicationResult coherentWave32 =
            simulateRequests(coherentRequests, true, 32u);
        const DeduplicationResult coherentWave64 =
            simulateRequests(coherentRequests, true, 64u);
        const DeduplicationResult coherentWave128 =
            simulateRequests(coherentRequests, true, 128u);
        assert(coherentWithoutWave.localCompareExchangeAttempts == 64u);
        assert(coherentWave32.localCompareExchangeAttempts == 2u);
        assert(coherentWave32.waveSuppressedRequests == 62u);
        assert(coherentWave64.localCompareExchangeAttempts == 1u);
        assert(coherentWave64.waveSuppressedRequests == 63u);
        assert(coherentWave128.localCompareExchangeAttempts == 1u);
        assert(coherentWave128.waveSuppressedRequests == 63u);
        assert(coherentWave32.publishedOwners ==
            coherentWave64.publishedOwners);
        assert(coherentWave64.publishedOwners ==
            coherentWave128.publishedOwners);

        const std::vector<uint32_t> boundaryRequests = {
            centerOwner,
            neighborOwner,
            centerOwner,
            coarseOwner,
            centerOwner
        };
        const DeduplicationResult boundaryWave2 =
            simulateRequests(boundaryRequests, true, 2u);
        // Indices 0, 2, and 4 occupy separate two-lane waves, so WaveMatch
        // cannot merge them even though the group hash still can.
        assert(boundaryWave2.waveSuppressedRequests == 0u);
        assert(boundaryWave2.localCompareExchangeAttempts ==
            boundaryRequests.size());
        std::vector<uint32_t> boundaryOwners =
            boundaryWave2.publishedOwners;
        sortAndDeduplicate(boundaryOwners);
        std::vector<uint32_t> boundaryReference = boundaryRequests;
        sortAndDeduplicate(boundaryReference);
        assert(boundaryOwners == boundaryReference);

        const uint32_t maximumValidOwner =
            SvsmClipmapCount * SvsmPagesPerClipmap - 1u;
        std::vector<uint32_t> partialWaveRequests(
            35u, centerOwner);
        partialWaveRequests.back() = maximumValidOwner;
        const DeduplicationResult partialWave32 =
            simulateRequests(partialWaveRequests, true, 32u);
        // The full first wave emits one center leader. The three-lane tail
        // emits one center leader plus the maximum valid owner.
        assert(partialWave32.localCompareExchangeAttempts == 3u);
        assert(partialWave32.waveSuppressedRequests == 32u);
        std::vector<uint32_t> partialWaveOwners =
            partialWave32.publishedOwners;
        sortAndDeduplicate(partialWaveOwners);
        std::vector<uint32_t> partialWaveReference =
            partialWaveRequests;
        sortAndDeduplicate(partialWaveReference);
        assert(partialWaveOwners == partialWaveReference);

        struct WaveRequest
        {
            uint32_t callOrdinal = 0u;
            uint32_t lane = 0u;
            uint32_t owner = 0u;
            bool active = true;
        };
        auto selectWaveLeaders =
            [](const std::vector<WaveRequest>& source,
                uint32_t waveSize)
            {
                assert(waveSize > 0u && waveSize <= 128u);
                std::vector<uint32_t> leaders;
                for (const WaveRequest& request : source)
                {
                    if (!request.active)
                        continue;
                    const uint32_t wave = request.lane / waveSize;
                    bool highestMatchingLane = true;
                    for (const WaveRequest& peer : source)
                    {
                        if (peer.active &&
                            peer.callOrdinal == request.callOrdinal &&
                            peer.lane / waveSize == wave &&
                            peer.owner == request.owner &&
                            peer.lane > request.lane)
                        {
                            highestMatchingLane = false;
                            break;
                        }
                    }
                    if (highestMatchingLane)
                        leaders.push_back(request.owner);
                }
                return leaders;
            };

        // Model two distinct dynamic RequestPage invocations explicitly. The
        // wave layer runs again for call 1, while the persistent group hash
        // still suppresses its repeated owner.
        std::vector<WaveRequest> multiCallRequests;
        for (uint32_t lane = 0u; lane < 64u; ++lane)
        {
            multiCallRequests.push_back({
                0u,
                lane,
                lane < 16u || lane >= 32u
                    ? centerOwner
                    : neighborOwner,
                true
            });
            multiCallRequests.push_back({
                1u, lane, centerOwner, true
            });
        }
        // Inactive lanes never contribute to WaveMatch. Include the maximum
        // valid owner in a partial active set to cover both conditions.
        multiCallRequests.push_back({
            2u, 0u, maximumValidOwner, true
        });
        multiCallRequests.push_back({
            2u, 1u, maximumValidOwner, false
        });

        const std::vector<uint32_t> multiCallWave32 =
            selectWaveLeaders(multiCallRequests, 32u);
        const std::vector<uint32_t> multiCallWave64 =
            selectWaveLeaders(multiCallRequests, 64u);
        const std::vector<uint32_t> multiCallWave128 =
            selectWaveLeaders(multiCallRequests, 128u);
        assert(multiCallWave32.size() == 6u);
        assert(multiCallWave64.size() == 4u);
        assert(multiCallWave128.size() == 4u);

        for (const std::vector<uint32_t>* leaders : {
                &multiCallWave32,
                &multiCallWave64,
                &multiCallWave128 })
        {
            DeduplicationResult multiCall =
                simulateRequests(*leaders, true);
            sortAndDeduplicate(multiCall.publishedOwners);
            assert((multiCall.publishedOwners ==
                std::vector<uint32_t>{
                    centerOwner,
                    neighborOwner,
                    maximumValidOwner
                }));
        }

        std::vector<uint32_t> multiCallReferenceRequests;
        for (const WaveRequest& request : multiCallRequests)
        {
            if (request.active)
                multiCallReferenceRequests.push_back(request.owner);
        }
        DeduplicationResult multiCallReference =
            simulateRequests(
                multiCallReferenceRequests, false, 128u);
        assert(multiCallReference.directGlobalWrites ==
            multiCallReferenceRequests.size());
        assert(multiCallReference.waveSuppressedRequests == 0u);

        // Stress bounded exhaustion with deterministic request order and
        // duplicates. The table is only a performance cache: every unique
        // request must still reach either one shared slot or the global path.
        std::vector<uint32_t> stressRequests;
        for (uint32_t level = 0u; level < SvsmClipmapCount; ++level)
        {
            for (uint32_t coordinate : { 0u, 1u, 31u, 62u, 63u })
            {
                const uint32_t owner = EncodeSvsmVirtualPageOwner(
                    { int32_t(coordinate),
                        int32_t(63u - coordinate) },
                    level);
                stressRequests.push_back(owner);
                stressRequests.push_back(owner);
            }
        }
        for (uint32_t owner = 0u; owner < 96u; ++owner)
            stressRequests.push_back(owner);
        uint32_t shuffleState = 0x12345678u;
        for (std::size_t index = stressRequests.size();
            index > 1u;
            --index)
        {
            shuffleState =
                shuffleState * 1664525u + 1013904223u;
            const std::size_t swapIndex =
                shuffleState % index;
            std::swap(
                stressRequests[index - 1u],
                stressRequests[swapIndex]);
        }

        std::vector<uint32_t> stressReference = stressRequests;
        sortAndDeduplicate(stressReference);
        DeduplicationResult stress =
            simulateRequests(stressRequests, true, 32u);
        sortAndDeduplicate(stress.publishedOwners);
        assert(stress.waveSuppressedRequests > 0u);
        assert(stress.probeLimitFailedOpen);
        assert(stress.publishedOwners == stressReference);

        DeduplicationResult stressWave64 =
            simulateRequests(stressRequests, true, 64u);
        sortAndDeduplicate(stressWave64.publishedOwners);
        assert(stressWave64.waveSuppressedRequests > 0u);
        assert(stressWave64.probeLimitFailedOpen);
        assert(stressWave64.publishedOwners == stressReference);
    }

    void TestPacketPageCompaction()
    {
        const uint32_t minimum =
            PackSvsmPacketPageCoordinate(3u, 5u);
        const uint32_t maximum =
            PackSvsmPacketPageCoordinate(6u, 9u);
        assert((UnpackSvsmPacketPageCoordinate(minimum) ==
            SvsmPageCoordinate{ 3, 5 }));
        assert(GetSvsmPacketPageListCapacity(
            minimum, maximum) == 20u);
        assert(GetSvsmPacketPageListCapacity(
            SvsmInvalidPacketPageBounds, maximum) == 0u);
        assert(GetSvsmPacketPageListCapacity(
            SvsmEmptyPacketPageBounds, maximum) == 0u);
        assert(GetSvsmPacketPageListCapacity(
            PackSvsmPacketPageCoordinate(7u, 5u),
            maximum) == 0u);

        // Table coordinates wrap independently from conservative local
        // packet bounds. Offset (63, 62) maps table (2, 3) to local (3, 5).
        assert(IsSvsmPacketPageInsideBounds(
            minimum,
            maximum,
            { 2, 3 },
            { 63, 62 }));
        assert(!IsSvsmPacketPageInsideBounds(
            minimum,
            maximum,
            { 1, 3 },
            { 63, 62 }));

        const std::array<SvsmPageCoordinate, 5> dirtyTablePages = {
            SvsmPageCoordinate{ 2, 3 },
            SvsmPageCoordinate{ 5, 7 },
            SvsmPageCoordinate{ 1, 3 },
            SvsmPageCoordinate{ 7, 9 },
            SvsmPageCoordinate{ 62, 61 }
        };
        uint32_t compactedCount = 0u;
        for (const SvsmPageCoordinate page : dirtyTablePages)
        {
            if (IsSvsmPacketPageInsideBounds(
                    minimum,
                    maximum,
                    page,
                    { 63, 62 }))
            {
                ++compactedCount;
            }
        }
        assert(compactedCount == 2u);
        assert(compactedCount <= GetSvsmPacketPageListCapacity(
            minimum, maximum));

        assert(ShouldScanSvsmPacketRectangleDirectly(true, 1u, 2u));
        assert(ShouldScanSvsmPacketRectangleDirectly(true, 32u, 64u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            false, 1u, 4096u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            true, 33u, 64u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            true, 0u, 64u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            true, 2u, 3u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            true, 64u, 64u));
        assert(!ShouldScanSvsmPacketRectangleDirectly(
            true, SvsmPagesPerClipmap, SvsmPagesPerClipmap));

        const SvsmPageCoordinate wrappedTablePage =
            SvsmPacketTablePageFromLocalPage(
                { 3, 5 }, { 63, 62 });
        assert((wrappedTablePage == SvsmPageCoordinate{ 2, 3 }));
        assert((SvsmPacketTablePageFromLocalPage(
            { 63, 63 }, { 1, 1 }) ==
            SvsmPageCoordinate{ 0, 0 }));

        assert(GetSvsmDirtyPageScatterInstanceCount(
            true, 0u) == 0u);
        assert(GetSvsmDirtyPageScatterInstanceCount(
            true, 1u) == 1u);
        assert(GetSvsmDirtyPageScatterInstanceCount(
            true, std::numeric_limits<uint32_t>::max()) == 1u);
        assert(GetSvsmDirtyPageScatterInstanceCount(
            false, 17u) == 17u);

        const SvsmDirtyPageRectangle emptyDirtyRectangle =
            DecodeSvsmDirtyPageRectangle({});
        assert(!emptyDirtyRectangle.valid);
        SvsmDirtyPageRectangleEncoding dirtyRectangleForward = {};
        dirtyRectangleForward = AccumulateSvsmDirtyPageRectangle(
            dirtyRectangleForward, { 63, 63 });
        dirtyRectangleForward = AccumulateSvsmDirtyPageRectangle(
            dirtyRectangleForward, { 0, 0 });
        const SvsmDirtyPageRectangle fullDirtyRectangle =
            DecodeSvsmDirtyPageRectangle(dirtyRectangleForward);
        assert(fullDirtyRectangle.valid);
        assert((fullDirtyRectangle.minimum ==
            SvsmPageCoordinate{ 0, 0 }));
        assert((fullDirtyRectangle.maximum ==
            SvsmPageCoordinate{ 63, 63 }));
        SvsmDirtyPageRectangleEncoding dirtyRectangleReverse = {};
        dirtyRectangleReverse = AccumulateSvsmDirtyPageRectangle(
            dirtyRectangleReverse, { 0, 0 });
        dirtyRectangleReverse = AccumulateSvsmDirtyPageRectangle(
            dirtyRectangleReverse, { 63, 63 });
        assert(dirtyRectangleReverse == dirtyRectangleForward);
        const SvsmDirtyPageRectangleEncoding singleDirtyPageEncoding =
            AccumulateSvsmDirtyPageRectangle({}, { 17, 29 });
        assert((singleDirtyPageEncoding ==
            SvsmDirtyPageRectangleEncoding{ 46u, 17u, 34u, 29u }));
        const SvsmDirtyPageRectangle singleDirtyPage =
            DecodeSvsmDirtyPageRectangle(singleDirtyPageEncoding);
        assert(singleDirtyPage.valid);
        assert((singleDirtyPage.minimum ==
            SvsmPageCoordinate{ 17, 29 }));
        assert(singleDirtyPage.minimum == singleDirtyPage.maximum);
        const SvsmDirtyPageRectangleEncoding unchangedDirtyRectangle =
            AccumulateSvsmDirtyPageRectangle(
                singleDirtyPageEncoding, { -1, 64 });
        assert(unchangedDirtyRectangle == singleDirtyPageEncoding);
        assert(!DecodeSvsmDirtyPageRectangle(
            SvsmDirtyPageRectangleEncoding{ 64u, 0u, 0u, 0u }).valid);
        const SvsmDirtyPageRectangle packetRectangle = {
            true, { 10, 20 }, { 30, 40 }
        };
        const SvsmDirtyPageRectangle scheduledRectangle = {
            true, { 25, 0 }, { 63, 29 }
        };
        const SvsmDirtyPageRectangle scatterIntersection =
            IntersectSvsmPageRectangles(
                packetRectangle, scheduledRectangle);
        assert(scatterIntersection.valid);
        assert((scatterIntersection.minimum ==
            SvsmPageCoordinate{ 25, 20 }));
        assert((scatterIntersection.maximum ==
            SvsmPageCoordinate{ 30, 29 }));
        assert(!IntersectSvsmPageRectangles(
            packetRectangle,
            { true, { 31, 41 }, { 63, 63 } }).valid);
        assert(!IntersectSvsmPageRectangles(
            packetRectangle, {}).valid);

        const SvsmPacketPageRectangle noScatterWork =
            ResolveSvsmScatterPacketRectangle(
                0u,
                SvsmInvalidPacketPageBounds,
                SvsmInvalidPacketPageBounds);
        assert(noScatterWork.packedMinimum ==
            SvsmEmptyPacketPageBounds);
        assert(noScatterWork.packedMaximum ==
            SvsmEmptyPacketPageBounds);
        const SvsmPacketPageRectangle malformedScatterBounds =
            ResolveSvsmScatterPacketRectangle(
                1u,
                SvsmEmptyPacketPageBounds,
                SvsmEmptyPacketPageBounds);
        assert((UnpackSvsmPacketPageCoordinate(
            malformedScatterBounds.packedMinimum) ==
            SvsmPageCoordinate{ 0, 0 }));
        assert((UnpackSvsmPacketPageCoordinate(
            malformedScatterBounds.packedMaximum) ==
            SvsmPageCoordinate{ 63, 63 }));
        const uint32_t validScatterMinimum =
            PackSvsmPacketPageCoordinate(7u, 9u);
        const uint32_t validScatterMaximum =
            PackSvsmPacketPageCoordinate(11u, 13u);
        const SvsmPacketPageRectangle validScatterBounds =
            ResolveSvsmScatterPacketRectangle(
                4u,
                validScatterMinimum,
                validScatterMaximum);
        assert(validScatterBounds.packedMinimum ==
            validScatterMinimum);
        assert(validScatterBounds.packedMaximum ==
            validScatterMaximum);

        const SvsmScatterVirtualTexelAddress texel127 =
            GetSvsmScatterVirtualTexelAddress(
                127u, 127u, { 63, 62 }, 2u);
        assert(texel127.valid);
        assert((texel127.localPage ==
            SvsmPageCoordinate{ 0, 0 }));
        assert((texel127.pageTexel ==
            SvsmPageCoordinate{ 127, 127 }));
        assert((texel127.tablePage ==
            SvsmPageCoordinate{ 63, 62 }));
        const SvsmScatterVirtualTexelAddress texel128 =
            GetSvsmScatterVirtualTexelAddress(
                128u, 128u, { 63, 62 }, 2u);
        assert(texel128.valid);
        assert((texel128.localPage ==
            SvsmPageCoordinate{ 1, 1 }));
        assert((texel128.pageTexel ==
            SvsmPageCoordinate{ 0, 0 }));
        assert((texel128.tablePage ==
            SvsmPageCoordinate{ 0, 63 }));
        const SvsmScatterVirtualTexelAddress texel8191 =
            GetSvsmScatterVirtualTexelAddress(
                8191u, 8191u, { 1, 1 }, 5u);
        assert(texel8191.valid);
        assert((texel8191.localPage ==
            SvsmPageCoordinate{ 63, 63 }));
        assert((texel8191.pageTexel ==
            SvsmPageCoordinate{ 127, 127 }));
        assert((texel8191.tablePage ==
            SvsmPageCoordinate{ 0, 0 }));
        assert(texel8191.owner == 5u * SvsmPagesPerClipmap);
        assert(!GetSvsmScatterVirtualTexelAddress(
            8192u, 0u, { 0, 0 }, 0u).valid);
        assert(!GetSvsmScatterVirtualTexelAddress(
            0u, 8192u, { 0, 0 }, 0u).valid);
        assert(!GetSvsmScatterVirtualTexelAddress(
            0u, 0u, { 0, 0 }, SvsmClipmapCount).valid);

        const uint32_t levelZeroOwner =
            EncodeSvsmVirtualPageOwner(wrappedTablePage, 0u);
        const uint32_t levelOneOwner =
            EncodeSvsmVirtualPageOwner(wrappedTablePage, 1u);
        assert(levelZeroOwner != levelOneOwner);
        SvsmPageMetadata scheduledPage;
        scheduledPage.physicalPage = 7u;
        scheduledPage.resident = true;
        scheduledPage.required = true;
        scheduledPage.dirty = true;
        assert(IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            levelZeroOwner,
            levelZeroOwner));
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            7u,
            levelZeroOwner,
            levelZeroOwner,
            levelZeroOwner));
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            levelZeroOwner,
            levelOneOwner));
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            levelZeroOwner,
            SvsmInvalidPhysicalPage));
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            levelOneOwner,
            levelZeroOwner));
        scheduledPage.dirty = false;
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            levelZeroOwner,
            levelZeroOwner));
        scheduledPage.dirty = true;
        scheduledPage.resident = false;
        assert(!IsSvsmPacketPageScheduledForRender(
            PackSvsmPageMetadata(scheduledPage),
            8u,
            levelZeroOwner,
            levelZeroOwner,
            levelZeroOwner));

        const uint32_t compactCount = 17u;
        const uint32_t failOpen =
            SvsmPacketPageRuntimeFailOpenBit | compactCount;
        assert((failOpen & SvsmPacketPageRuntimeFailOpenBit) != 0u);
        assert((failOpen & SvsmPacketPageRuntimeCountMask) ==
            compactCount);
        const uint32_t perPage =
            SvsmPacketPageRuntimePerPageBit | compactCount;
        assert((perPage & SvsmPacketPageRuntimePerPageBit) != 0u);
        assert((perPage & SvsmPacketPageRuntimeFailOpenBit) == 0u);
        assert((perPage & SvsmPacketPageRuntimeCountMask) ==
            compactCount);
        assert(!IsSvsmDirtyPageScatterPerPageRuntimeActive(
            false, perPage));
        assert(IsSvsmDirtyPageScatterPerPageRuntimeActive(
            true, perPage));
        assert(IsSvsmDirtyPageScatterPerPageRuntimeActive(
            true,
            perPage | SvsmPacketPageRuntimeFailOpenBit));
        assert(ShouldClipSvsmDirtyPagePerPageRasterToPacketBounds(
            true, perPage));
        assert(!ShouldClipSvsmDirtyPagePerPageRasterToPacketBounds(
            true,
            perPage | SvsmPacketPageRuntimeFailOpenBit));
        assert(!IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::DenseReference, true, true, true, true));
        assert(!IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::SparseCached, false, true, true, true));
        assert(!IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::SparseCached, true, false, true, true));
        assert(!IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::SparseCached, true, true, false, true));
        assert(!IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::SparseCached, true, true, true, false));
        assert(IsSvsmDirtyPageScatterOptimizationActive(
            SvsmMode::SparseCached, true, true, true, true));
        assert(!IsSvsmDirtyPageScatterSafetyBounded(
            false, true, 4u, 4u));
        assert(IsSvsmDirtyPageScatterSafetyBounded(
            true, false, 4u, 4u));
        assert(IsSvsmDirtyPageScatterSafetyBounded(
            true, true, 0u, 4u));
        assert(IsSvsmDirtyPageScatterSafetyBounded(
            true, true, 4u, 4u));
        assert(IsSvsmDirtyPageScatterSafetyBounded(
            true, true, 4u, 5u));
        assert(IsSvsmDirtyPageScatterSafetyBounded(
            true, true, 2u, 8u));
        assert(IsSvsmDirtyPageScatterSafetyBounded(
            true, true,
            std::numeric_limits<uint32_t>::max(),
            1u));
        assert(!IsSvsmDirtyPageScatterSafetyBounded(
            true, true,
            std::numeric_limits<uint32_t>::max(),
            SvsmMaximumDirtyPageScatterAmplification + 1u));
        assert(!ShouldUseSvsmDirtyPageScatterPerPageFallback(
            false, 4096u, 4u, 4u));
        assert(!ShouldUseSvsmDirtyPageScatterPerPageFallback(
            true, 16u, 4u, 4u));
        assert(ShouldUseSvsmDirtyPageScatterPerPageFallback(
            true, 17u, 4u, 4u));
        assert(!ShouldUseSvsmDirtyPageScatterPerPageFallback(
            true, 4096u, 0u, 4u));
        assert(!ShouldUseSvsmDirtyPageScatterPerPageFallback(
            true, 4096u, 4u, 0u));
        assert(!ShouldUseSvsmDirtyPageScatterPerPageFallback(
            true,
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max()));
        assert(SvsmLevelHasWorkDispatchGate == 1u);

        constexpr uint64_t maximumPacketPageDispatchGroupCount =
            uint64_t(SvsmMaximumDispatchGroupsPerDimension) *
            uint64_t(SvsmMaximumDispatchGroupsPerDimension);
        constexpr uint32_t maximumExactPacketCount =
            uint32_t(maximumPacketPageDispatchGroupCount);
        assert(CanDispatchSvsmPacketPageCulling(0u, false));
        assert(CanDispatchSvsmPacketPageCulling(
            SvsmMaximumDispatchGroupsPerDimension + 1u, false));
        assert(CanDispatchSvsmPacketPageCulling(
            maximumExactPacketCount, false));
        assert(!CanDispatchSvsmPacketPageCulling(
            maximumExactPacketCount + 1u, false));
        assert(!CanDispatchSvsmPacketPageCulling(
            std::numeric_limits<uint32_t>::max(), false));
        assert(!CanDispatchSvsmPacketPageCulling(
            std::numeric_limits<uint32_t>::max(), true));
        assert((GetSvsmIndirectFillDispatchDimensions(
            0u, false, false) ==
            SvsmDispatchDimensions{ 0u, 0u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            65u, false, false) ==
            SvsmDispatchDimensions{ 2u, 1u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            65u, true, true) ==
            SvsmDispatchDimensions{ 65u, 1u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            SvsmMaximumDispatchGroupsPerDimension,
            true, false) == SvsmDispatchDimensions{ 65535u, 1u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            SvsmMaximumDispatchGroupsPerDimension + 1u,
            true, false) == SvsmDispatchDimensions{ 65535u, 2u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            SvsmMaximumDispatchGroupsPerDimension *
                SvsmPacketFillThreadsPerGroup + 1u,
            false, false) == SvsmDispatchDimensions{ 65535u, 2u }));
        assert((GetSvsmIndirectFillDispatchDimensions(
            maximumExactPacketCount,
            true, false) == SvsmDispatchDimensions{ 65535u, 65535u }));
        const SvsmDispatchDimensions overflowingExactDispatch =
            GetSvsmIndirectFillDispatchDimensions(
                maximumExactPacketCount + 1u,
                true,
                false);
        assert(overflowingExactDispatch.groupsX ==
            SvsmMaximumDispatchGroupsPerDimension);
        assert(overflowingExactDispatch.groupsY ==
            SvsmMaximumDispatchGroupsPerDimension + 1u);
        const SvsmDispatchDimensions maximumScatterDispatch =
            GetSvsmIndirectFillDispatchDimensions(
                std::numeric_limits<uint32_t>::max(),
                true,
                true);
        assert(maximumScatterDispatch.groupsX ==
            SvsmMaximumDispatchGroupsPerDimension);
        assert(maximumScatterDispatch.groupsY >
            SvsmMaximumDispatchGroupsPerDimension);
        const SvsmDispatchDimensions maximumFallbackDispatch =
            GetSvsmIndirectFillDispatchDimensions(
                std::numeric_limits<uint32_t>::max(),
                false,
                false);
        assert(maximumFallbackDispatch.groupsX <=
            SvsmMaximumDispatchGroupsPerDimension);
        assert(maximumFallbackDispatch.groupsY <=
            SvsmMaximumDispatchGroupsPerDimension);
        assert(CanUseSvsmStaticPacketBounds(false, false, false));
        assert(!CanUseSvsmStaticPacketBounds(true, false, false));
        assert(!CanUseSvsmStaticPacketBounds(false, true, false));
        assert(!CanUseSvsmStaticPacketBounds(false, false, true));
        assert(!ShouldPrepareSvsmRenderPacketsForClipmap(0u, 1u));
        assert(ShouldPrepareSvsmRenderPacketsForClipmap(1u, 1u));
        assert(ShouldPrepareSvsmRenderPacketsForClipmap(5u, 2u));
        assert(!ShouldPrepareSvsmRenderPacketsForClipmap(6u, 0u));
        assert(ShouldPrepareSvsmRenderPacketsForClipmap(5u, 99u));

        const float pageFiveCenter =
            float(SvsmPageSize * 5u) +
            float(SvsmPageSize) * 0.5f;
        const SvsmPacketPageRectangle tinyCaster =
            GetSvsmPacketPageRectangle(
                pageFiveCenter,
                pageFiveCenter,
                pageFiveCenter,
                pageFiveCenter);
        assert((UnpackSvsmPacketPageCoordinate(
            tinyCaster.packedMinimum) ==
            SvsmPageCoordinate{ 5, 5 }));
        assert((UnpackSvsmPacketPageCoordinate(
            tinyCaster.packedMaximum) ==
            SvsmPageCoordinate{ 5, 5 }));
        const SvsmPacketPageRectangle boundaryCaster =
            GetSvsmPacketPageRectangle(
                float(SvsmPageSize),
                float(SvsmPageSize),
                float(SvsmPageSize),
                float(SvsmPageSize));
        assert((UnpackSvsmPacketPageCoordinate(
            boundaryCaster.packedMinimum) ==
            SvsmPageCoordinate{ 0, 0 }));
        assert((UnpackSvsmPacketPageCoordinate(
            boundaryCaster.packedMaximum) ==
            SvsmPageCoordinate{ 1, 1 }));
        const SvsmPacketPageRectangle negativeGuardEdge =
            GetSvsmPacketPageRectangle(
                float(SvsmPageSize) + SvsmPacketBoundsTexelHalo,
                pageFiveCenter,
                float(SvsmPageSize) + SvsmPacketBoundsTexelHalo,
                pageFiveCenter);
        assert((UnpackSvsmPacketPageCoordinate(
            negativeGuardEdge.packedMinimum) ==
            SvsmPageCoordinate{ 0, 5 }));
        const SvsmPacketPageRectangle positiveGuardEdge =
            GetSvsmPacketPageRectangle(
                float(SvsmPageSize) - SvsmPacketBoundsTexelHalo,
                pageFiveCenter,
                float(SvsmPageSize) - SvsmPacketBoundsTexelHalo,
                pageFiveCenter);
        assert((UnpackSvsmPacketPageCoordinate(
            positiveGuardEdge.packedMaximum) ==
            SvsmPageCoordinate{ 1, 5 }));
        const SvsmPacketPageRectangle outsideCaster =
            GetSvsmPacketPageRectangle(-4.f, -4.f, -2.f, -2.f);
        assert(outsideCaster.packedMinimum ==
            SvsmEmptyPacketPageBounds);
        assert(outsideCaster.packedMaximum ==
            SvsmEmptyPacketPageBounds);
        const SvsmPacketPageRectangle invalidCaster =
            GetSvsmPacketPageRectangle(2.f, 0.f, 1.f, 1.f);
        assert(invalidCaster.packedMinimum ==
            SvsmInvalidPacketPageBounds);
        assert(invalidCaster.packedMaximum ==
            SvsmInvalidPacketPageBounds);
        assert(RequiresSvsmPacketPageModeTransition(
            true, false, true, true, false,
            true, true, true));
        assert(RequiresSvsmPacketPageModeTransition(
            true, true, false, false, false,
            true, true, true));
        assert(RequiresSvsmPacketPageModeTransition(
            true, true, true, false, false,
            true, true, true));
        assert(!RequiresSvsmPacketPageModeTransition(
            true, true, true, false, true,
            true, true, true));
        assert(!RequiresSvsmPacketPageModeTransition(
            true, true, true, true, false,
            true, true, true));
        assert(!RequiresSvsmPacketPageModeTransition(
            false, true, false, false, false,
            false, false, true));
        assert(RequiresSvsmPacketPageModeTransition(
            true, true, true, true, true,
            false, false, false));
        assert(RequiresSvsmPacketPageModeTransition(
            true, true, true, true, true,
            true, true, false));
    }

    void TestTightLocalizedInvalidationProjection()
    {
        auto makeClipBox = [](
            float minimumX,
            float minimumY,
            float minimumDepth,
            float maximumX,
            float maximumY,
            float maximumDepth) {
            std::array<std::array<float, 4u>, 8u> corners{};
            for (uint32_t corner = 0u; corner < corners.size(); ++corner)
            {
                corners[corner] = {
                    (corner & 1u) != 0u ? maximumX : minimumX,
                    (corner & 2u) != 0u ? maximumY : minimumY,
                    (corner & 4u) != 0u
                        ? maximumDepth
                        : minimumDepth,
                    1.f
                };
            }
            return corners;
        };

        const SvsmTightInvalidationProjection ordinary =
            ProjectSvsmClipCornersForInvalidation(
                makeClipBox(
                    -0.25f,
                    -0.125f,
                    0.25f,
                    0.25f,
                    0.125f,
                    0.75f));
        assert(ordinary.valid);
        assert(ordinary.overlapsDepthRange);
        assert(ordinary.minimumReverseDepth == 0.25f);
        assert(ordinary.maximumReverseDepth == 0.75f);
        assert(ordinary.pages.packedMinimum !=
            SvsmInvalidPacketPageBounds);
        assert(ordinary.pages.packedMinimum !=
            SvsmEmptyPacketPageBounds);

        const SvsmTightInvalidationProjection behindFarPlane =
            ProjectSvsmClipCornersForInvalidation(
                makeClipBox(
                    -0.25f,
                    -0.25f,
                    -0.2f,
                    0.25f,
                    0.25f,
                    -0.0001f));
        assert(behindFarPlane.valid);
        assert(!behindFarPlane.overlapsDepthRange);
        assert(behindFarPlane.pages.packedMinimum ==
            SvsmEmptyPacketPageBounds);
        assert(behindFarPlane.pages.packedMaximum ==
            SvsmEmptyPacketPageBounds);

        const SvsmTightInvalidationProjection beyondNearPlane =
            ProjectSvsmClipCornersForInvalidation(
                makeClipBox(
                    -0.25f,
                    -0.25f,
                    1.0001f,
                    0.25f,
                    0.25f,
                    1.2f));
        assert(beyondNearPlane.valid);
        assert(!beyondNearPlane.overlapsDepthRange);
        assert(beyondNearPlane.pages.packedMinimum ==
            SvsmEmptyPacketPageBounds);

        const float conservativeFar =
            std::nextafter(
                0.f,
                -std::numeric_limits<float>::infinity());
        const float conservativeNear =
            std::nextafter(
                1.f,
                std::numeric_limits<float>::infinity());
        const SvsmTightInvalidationProjection farBoundary =
            ProjectSvsmClipCornersForInvalidation(
                makeClipBox(
                    -0.1f,
                    -0.1f,
                    conservativeFar,
                    0.1f,
                    0.1f,
                    conservativeFar));
        const SvsmTightInvalidationProjection nearBoundary =
            ProjectSvsmClipCornersForInvalidation(
                makeClipBox(
                    -0.1f,
                    -0.1f,
                    conservativeNear,
                    0.1f,
                    0.1f,
                    conservativeNear));
        assert(farBoundary.valid &&
            farBoundary.overlapsDepthRange);
        assert(nearBoundary.valid &&
            nearBoundary.overlapsDepthRange);

        auto malformedCorners = makeClipBox(
            -0.1f, -0.1f, 0.25f,
            0.1f, 0.1f, 0.75f);
        malformedCorners[3u][3u] = 0.f;
        assert(!ProjectSvsmClipCornersForInvalidation(
            malformedCorners).valid);
        malformedCorners = makeClipBox(
            -0.1f, -0.1f, 0.25f,
            0.1f, 0.1f, 0.75f);
        malformedCorners[5u][0u] =
            std::numeric_limits<float>::quiet_NaN();
        assert(!ProjectSvsmClipCornersForInvalidation(
            malformedCorners).valid);

        uint32_t randomState = 0x61c88647u;
        auto randomUnit = [&randomState]() {
            randomState =
                randomState * 1664525u + 1013904223u;
            return float(randomState >> 8u) /
                float(1u << 24u);
        };
        constexpr float Pi = 3.14159265358979323846f;
        uint32_t strictlyTighterCases = 0u;
        for (uint32_t iteration = 0u;
            iteration < 1024u;
            ++iteration)
        {
            const float objectAngle = randomUnit() * Pi;
            const float lightAngle = randomUnit() * Pi;
            const float objectCos = std::cos(objectAngle);
            const float objectSin = std::sin(objectAngle);
            const float lightCos = std::cos(lightAngle);
            const float lightSin = std::sin(lightAngle);
            const float extentX = 0.25f + randomUnit() * 3.75f;
            const float extentY = 0.25f + randomUnit() * 3.75f;
            const float extentZ = 0.1f + randomUnit();
            const float translationX =
                (randomUnit() * 2.f - 1.f) * 10.f;
            const float translationY =
                (randomUnit() * 2.f - 1.f) * 10.f;
            const float translationZ =
                (randomUnit() * 2.f - 1.f) * 2.f;

            std::array<std::array<float, 3u>, 8u> worldCorners{};
            std::array<float, 3u> worldMinimum = {
                std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity()
            };
            std::array<float, 3u> worldMaximum = {
                -std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity()
            };
            for (uint32_t corner = 0u;
                corner < worldCorners.size();
                ++corner)
            {
                const float objectX =
                    (corner & 1u) != 0u ? extentX : -extentX;
                const float objectY =
                    (corner & 2u) != 0u ? extentY : -extentY;
                const float objectZ =
                    (corner & 4u) != 0u ? extentZ : -extentZ;
                worldCorners[corner] = {
                    objectCos * objectX -
                        objectSin * objectY +
                        translationX,
                    objectSin * objectX +
                        objectCos * objectY +
                        translationY,
                    objectZ + translationZ
                };
                for (uint32_t axis = 0u; axis < 3u; ++axis)
                {
                    worldMinimum[axis] = std::min(
                        worldMinimum[axis],
                        worldCorners[corner][axis]);
                    worldMaximum[axis] = std::max(
                        worldMaximum[axis],
                        worldCorners[corner][axis]);
                }
            }

            auto projectWorldCorners = [&](
                const std::array<std::array<float, 3u>, 8u>&
                    positions) {
                std::array<std::array<float, 4u>, 8u> clips{};
                for (uint32_t corner = 0u;
                    corner < positions.size();
                    ++corner)
                {
                    const float worldX = positions[corner][0u];
                    const float worldY = positions[corner][1u];
                    const float lightX =
                        lightCos * worldX + lightSin * worldY;
                    const float lightY =
                        -lightSin * worldX + lightCos * worldY;
                    clips[corner] = {
                        lightX * 0.025f,
                        lightY * 0.025f,
                        0.5f + positions[corner][2u] * 0.05f,
                        1.f
                    };
                }
                return ProjectSvsmClipCornersForInvalidation(clips);
            };

            std::array<std::array<float, 3u>, 8u> looseCorners{};
            for (uint32_t corner = 0u;
                corner < looseCorners.size();
                ++corner)
            {
                looseCorners[corner] = {
                    (corner & 1u) != 0u
                        ? worldMaximum[0u]
                        : worldMinimum[0u],
                    (corner & 2u) != 0u
                        ? worldMaximum[1u]
                        : worldMinimum[1u],
                    (corner & 4u) != 0u
                        ? worldMaximum[2u]
                        : worldMinimum[2u]
                };
            }

            const SvsmTightInvalidationProjection tight =
                projectWorldCorners(worldCorners);
            const SvsmTightInvalidationProjection loose =
                projectWorldCorners(looseCorners);
            assert(tight.valid && loose.valid);
            assert(tight.overlapsDepthRange &&
                loose.overlapsDepthRange);
            assert(tight.pages.packedMinimum !=
                SvsmEmptyPacketPageBounds);
            assert(loose.pages.packedMinimum !=
                SvsmEmptyPacketPageBounds);

            const SvsmPageCoordinate tightMinimum =
                UnpackSvsmPacketPageCoordinate(
                    tight.pages.packedMinimum);
            const SvsmPageCoordinate tightMaximum =
                UnpackSvsmPacketPageCoordinate(
                    tight.pages.packedMaximum);
            const SvsmPageCoordinate looseMinimum =
                UnpackSvsmPacketPageCoordinate(
                    loose.pages.packedMinimum);
            const SvsmPageCoordinate looseMaximum =
                UnpackSvsmPacketPageCoordinate(
                    loose.pages.packedMaximum);
            assert(tightMinimum.x >= looseMinimum.x);
            assert(tightMinimum.y >= looseMinimum.y);
            assert(tightMaximum.x <= looseMaximum.x);
            assert(tightMaximum.y <= looseMaximum.y);
            strictlyTighterCases +=
                tightMinimum.x > looseMinimum.x ||
                tightMinimum.y > looseMinimum.y ||
                tightMaximum.x < looseMaximum.x ||
                tightMaximum.y < looseMaximum.y;
        }
        assert(strictlyTighterCases > 256u);
    }

    void TestScheduledPageTileMaskHierarchy()
    {
        constexpr uint32_t Generation = 19u;
        assert(ShouldUseSvsmScheduledPageTileMask(
            true, true, false, true, Generation));
        assert(!ShouldUseSvsmScheduledPageTileMask(
            false, true, false, true, Generation));
        assert(!ShouldUseSvsmScheduledPageTileMask(
            true, false, false, true, Generation));
        assert(!ShouldUseSvsmScheduledPageTileMask(
            true, true, false, false, Generation));
        assert(ShouldUseSvsmScheduledPageTileMask(
            true, true, true, true, Generation));
        assert(!ShouldUseSvsmScheduledPageTileMask(
            true, true, false, false, Generation));
        assert(!ShouldUseSvsmScheduledPageTileMask(
            true, true, false, true, 0u));
        using PageGrid =
            std::array<bool, SvsmPagesPerClipmap>;
        struct ScheduledGrid
        {
            PageGrid any{};
            PageGrid staticPages{};
        };

        const auto pageIndex = [](uint32_t x, uint32_t y) {
            return y * SvsmPagesPerAxis + x;
        };
        const auto buildMask = [&](
            const ScheduledGrid& grid,
            SvsmPageCoordinate offset) {
            SvsmScheduledPageTileMask mask;
            mask.generation = Generation;
            for (uint32_t y = 0u; y < SvsmPagesPerAxis; ++y)
            {
                for (uint32_t x = 0u;
                    x < SvsmPagesPerAxis;
                    ++x)
                {
                    const uint32_t index = pageIndex(x, y);
                    if (!grid.any[index])
                        continue;
                    const SvsmPageCoordinate tablePage =
                        SvsmPacketTablePageFromLocalPage(
                            { int32_t(x), int32_t(y) },
                            offset);
                    mask = AddSvsmScheduledPageTile(
                        mask,
                        tablePage,
                        offset,
                        grid.staticPages[index]);
                }
            }
            return mask;
        };
        const auto buildPrefix = [&](
            const PageGrid& grid) {
            std::array<uint32_t,
                (SvsmPagesPerAxis + 1u) *
                    (SvsmPagesPerAxis + 1u)> prefix{};
            constexpr uint32_t stride =
                SvsmPagesPerAxis + 1u;
            for (uint32_t y = 0u;
                y < SvsmPagesPerAxis;
                ++y)
            {
                for (uint32_t x = 0u;
                    x < SvsmPagesPerAxis;
                    ++x)
                {
                    prefix[(y + 1u) * stride + x + 1u] =
                        uint32_t(grid[pageIndex(x, y)]) +
                        prefix[y * stride + x + 1u] +
                        prefix[(y + 1u) * stride + x] -
                        prefix[y * stride + x];
                }
            }
            return prefix;
        };
        const auto exactIntersects = [](
            const auto& prefix,
            uint32_t minimumX,
            uint32_t minimumY,
            uint32_t maximumX,
            uint32_t maximumY) {
            constexpr uint32_t stride =
                SvsmPagesPerAxis + 1u;
            const uint32_t x0 = minimumX;
            const uint32_t y0 = minimumY;
            const uint32_t x1 = maximumX + 1u;
            const uint32_t y1 = maximumY + 1u;
            return prefix[y1 * stride + x1] -
                    prefix[y0 * stride + x1] -
                    prefix[y1 * stride + x0] +
                    prefix[y0 * stride + x0] !=
                0u;
        };
        const auto checkRectangle = [&](
            const SvsmScheduledPageTileMask& mask,
            const auto& anyPrefix,
            const auto& staticPrefix,
            uint32_t minimumX,
            uint32_t minimumY,
            uint32_t maximumX,
            uint32_t maximumY) {
            const uint32_t packedMinimum =
                PackSvsmPacketPageCoordinate(
                    minimumX, minimumY);
            const uint32_t packedMaximum =
                PackSvsmPacketPageCoordinate(
                    maximumX, maximumY);
            for (uint32_t staticCaster = 0u;
                staticCaster < 2u;
                ++staticCaster)
            {
                const bool exact = exactIntersects(
                    staticCaster ? staticPrefix : anyPrefix,
                    minimumX,
                    minimumY,
                    maximumX,
                    maximumY);
                const SvsmScheduledPageTileMaskQuery query =
                    QuerySvsmScheduledPageTileMask(
                        mask,
                        Generation,
                        packedMinimum,
                        packedMaximum,
                        staticCaster != 0u,
                        true);
                const bool optimizedFinal =
                    query !=
                        SvsmScheduledPageTileMaskQuery::Reject &&
                    exact;
                assert(optimizedFinal == exact);
                if (exact)
                {
                    assert(query ==
                        SvsmScheduledPageTileMaskQuery::Positive);
                }
            }
        };

        std::array<ScheduledGrid, 5> patterns{};
        patterns[1].any[pageIndex(32u, 31u)] = true;
        patterns[1].staticPages[pageIndex(32u, 31u)] = true;
        for (uint32_t y = 0u; y < SvsmPagesPerAxis; ++y)
        {
            for (uint32_t x = 0u;
                x < SvsmPagesPerAxis;
                ++x)
            {
                const uint32_t index = pageIndex(x, y);
                patterns[2].any[index] = true;
                patterns[2].staticPages[index] =
                    ((x + y) & 1u) == 0u;
                patterns[3].any[index] =
                    ((x + y) & 1u) == 0u;
                patterns[3].staticPages[index] =
                    patterns[3].any[index] &&
                    ((x * 3u + y) % 5u) == 0u;
                patterns[4].any[index] =
                    x >= 27u && x <= 36u &&
                    y >= 29u && y <= 38u;
                patterns[4].staticPages[index] =
                    x >= 30u && x <= 33u &&
                    y >= 31u && y <= 35u;
            }
        }

        constexpr std::array<SvsmPageCoordinate, 3> offsets = {
            SvsmPageCoordinate{ 0, 0 },
            SvsmPageCoordinate{ 63, 62 },
            SvsmPageCoordinate{ 17, 41 }
        };
        constexpr std::array<uint32_t, 8> edges = {
            0u, 7u, 8u, 31u, 32u, 55u, 56u, 63u
        };
        for (const ScheduledGrid& pattern : patterns)
        {
            const auto anyPrefix = buildPrefix(pattern.any);
            const auto staticPrefix =
                buildPrefix(pattern.staticPages);
            for (SvsmPageCoordinate offset : offsets)
            {
                const SvsmScheduledPageTileMask mask =
                    buildMask(pattern, offset);
                for (uint32_t minimumX : edges)
                {
                    for (uint32_t minimumY : edges)
                    {
                        for (uint32_t maximumX : edges)
                        {
                            for (uint32_t maximumY : edges)
                            {
                                if (maximumX < minimumX ||
                                    maximumY < minimumY)
                                {
                                    continue;
                                }
                                checkRectangle(
                                    mask,
                                    anyPrefix,
                                    staticPrefix,
                                    minimumX,
                                    minimumY,
                                    maximumX,
                                    maximumY);
                            }
                        }
                    }
                }
            }
        }

        // Exhaust every valid rectangle against a deterministic irregular
        // schedule. The hierarchy may produce tile-granularity positives,
        // but composing it with the exact scan must equal brute force.
        ScheduledGrid randomPattern;
        uint32_t randomState = 0x6d2b79f5u;
        for (uint32_t index = 0u;
            index < SvsmPagesPerClipmap;
            ++index)
        {
            randomState =
                randomState * 1664525u + 1013904223u;
            randomPattern.any[index] =
                (randomState & 7u) == 0u;
            randomPattern.staticPages[index] =
                randomPattern.any[index] &&
                (randomState & 0x30u) == 0u;
        }
        const SvsmScheduledPageTileMask randomMask =
            buildMask(randomPattern, { 63, 62 });
        const auto randomAnyPrefix =
            buildPrefix(randomPattern.any);
        const auto randomStaticPrefix =
            buildPrefix(randomPattern.staticPages);
        for (uint32_t minimumY = 0u;
            minimumY < SvsmPagesPerAxis;
            ++minimumY)
        {
            for (uint32_t minimumX = 0u;
                minimumX < SvsmPagesPerAxis;
                ++minimumX)
            {
                for (uint32_t maximumY = minimumY;
                    maximumY < SvsmPagesPerAxis;
                    ++maximumY)
                {
                    for (uint32_t maximumX = minimumX;
                        maximumX < SvsmPagesPerAxis;
                        ++maximumX)
                    {
                        checkRectangle(
                            randomMask,
                            randomAnyPrefix,
                            randomStaticPrefix,
                            minimumX,
                            minimumY,
                            maximumX,
                            maximumY);
                    }
                }
            }
        }

        assert(QuerySvsmScheduledPageTileMask(
            randomMask,
            Generation + 1u,
            PackSvsmPacketPageCoordinate(0u, 0u),
            PackSvsmPacketPageCoordinate(63u, 63u),
            false,
            true) ==
            SvsmScheduledPageTileMaskQuery::FailOpen);
        assert(QuerySvsmScheduledPageTileMask(
            randomMask,
            0u,
            PackSvsmPacketPageCoordinate(0u, 0u),
            PackSvsmPacketPageCoordinate(63u, 63u),
            false,
            true) ==
            SvsmScheduledPageTileMaskQuery::FailOpen);
        SvsmScheduledPageTileMask invalidClassMask = randomMask;
        invalidClassMask.anyLow &= ~1u;
        invalidClassMask.staticLow |= 1u;
        assert(QuerySvsmScheduledPageTileMask(
            invalidClassMask,
            Generation,
            PackSvsmPacketPageCoordinate(0u, 0u),
            PackSvsmPacketPageCoordinate(63u, 63u),
            true,
            true) ==
            SvsmScheduledPageTileMaskQuery::FailOpen);
        assert(QuerySvsmScheduledPageTileMask(
            randomMask,
            Generation,
            SvsmInvalidPacketPageBounds,
            PackSvsmPacketPageCoordinate(63u, 63u),
            false,
            true) ==
            SvsmScheduledPageTileMaskQuery::FailOpen);
        SvsmScheduledPageTileMask invalidEntryMask;
        invalidEntryMask.generation = Generation;
        invalidEntryMask = AddSvsmScheduledPageTile(
            invalidEntryMask,
            { -1, 0 },
            { 0, 0 },
            false);
        assert(QuerySvsmScheduledPageTileMask(
            invalidEntryMask,
            Generation,
            PackSvsmPacketPageCoordinate(0u, 0u),
            PackSvsmPacketPageCoordinate(63u, 63u),
            false,
            true) ==
            SvsmScheduledPageTileMaskQuery::FailOpen);
        // Without paired depth, a static-class packet intentionally queries
        // the any-dirty mask and retains the original one-layer behavior.
        assert(QuerySvsmScheduledPageTileMask(
            patterns[1].any[pageIndex(32u, 31u)]
                ? buildMask(patterns[1], { 0, 0 })
                : SvsmScheduledPageTileMask{},
            Generation,
            PackSvsmPacketPageCoordinate(32u, 31u),
            PackSvsmPacketPageCoordinate(32u, 31u),
            true,
            false) ==
            SvsmScheduledPageTileMaskQuery::Positive);
    }

    void TestStaticDepthPageHierarchy()
    {
        static_assert(SvsmStaticDepthHierarchyBaseCellWidth == 16u);
        static_assert(SvsmStaticDepthHierarchyBaseAxis == 8u);
        static_assert(SvsmStaticDepthHierarchyBaseOffset == 0u);
        static_assert(SvsmStaticDepthHierarchyBaseCount == 64u);
        static_assert(SvsmStaticDepthHierarchyLevelOneOffset == 64u);
        static_assert(SvsmStaticDepthHierarchyLevelOneCount == 16u);
        static_assert(SvsmStaticDepthHierarchyLevelTwoOffset == 80u);
        static_assert(SvsmStaticDepthHierarchyLevelTwoCount == 4u);
        static_assert(SvsmStaticDepthHierarchyRootOffset == 84u);
        static_assert(SvsmStaticDepthHierarchyTagOffset == 85u);
        static_assert(SvsmStaticDepthHierarchyWordsPerPage == 86u);
        static_assert(
            SvsmStaticDepthHierarchyBuiltPageCounter + 1u ==
            SvsmReceiverPageMaskCounterBase);

        const uint32_t owner =
            SvsmClipmapCount * SvsmPagesPerClipmap - 1u;
        const uint32_t tag =
            PackSvsmStaticDepthHierarchyTag(owner, 17u);
        assert(IsSvsmStaticDepthHierarchyTagValid(tag, owner));
        assert(!IsSvsmStaticDepthHierarchyTagValid(tag, owner - 1u));
        assert(UnpackSvsmStaticDepthHierarchyTagOwner(tag) == owner);
        assert(UnpackSvsmStaticDepthHierarchyTagEpoch(tag) == 17u);
        assert(!IsSvsmStaticDepthHierarchyTagValid(
            PackSvsmStaticDepthHierarchyTag(owner, 0u),
            owner));

        std::array<
            uint32_t,
            SvsmStaticDepthHierarchyBaseCount> baseCells;
        baseCells.fill(0x3f400000u);
        baseCells[0u] = 0x3f000000u;
        baseCells[9u] = 0x3e800000u;
        baseCells[63u] = 0x3f200000u;
        const SvsmStaticDepthHierarchyWords hierarchy =
            BuildSvsmStaticDepthHierarchyFromBaseCells(
                baseCells, tag);
        assert(hierarchy[SvsmStaticDepthHierarchyBaseOffset] ==
            0x3f000000u);
        assert(hierarchy[
            SvsmStaticDepthHierarchyLevelOneOffset] ==
            0x3e800000u);
        assert(hierarchy[
            SvsmStaticDepthHierarchyLevelTwoOffset] ==
            0x3e800000u);
        assert(hierarchy[SvsmStaticDepthHierarchyRootOffset] ==
            0x3e800000u);
        assert(hierarchy[SvsmStaticDepthHierarchyTagOffset] == tag);
        assert(QuerySvsmStaticDepthHierarchyBaseCellRegion(
            hierarchy, 0u, 0u, 0u, 0u) == 0x3f000000u);
        assert(QuerySvsmStaticDepthHierarchyBaseCellRegion(
            hierarchy, 1u, 1u, 1u, 1u) == 0x3e800000u);
        assert(QuerySvsmStaticDepthHierarchyBaseCellRegion(
            hierarchy, 0u, 0u, 3u, 3u) == 0x3e800000u);
        assert(QuerySvsmStaticDepthHierarchyBaseCellRegion(
            hierarchy, 0u, 0u, 7u, 7u) == 0x3e800000u);
        assert(QuerySvsmStaticDepthHierarchyBaseCellRegion(
            hierarchy, 7u, 7u, 6u, 7u) ==
            std::numeric_limits<uint32_t>::max());
        assert(QuerySvsmStaticDepthHierarchyRegion(
            hierarchy, 0u, 0u, 3u, 3u) == 0x3e800000u);
        assert(QuerySvsmStaticDepthHierarchyRegion(
            hierarchy, 0u, 0u, 7u, 7u) == 0x3e800000u);
        assert(QuerySvsmStaticDepthHierarchyRegion(
            hierarchy, 7u, 7u, 6u, 7u) ==
            std::numeric_limits<uint32_t>::max());

        // Mirror every rectangular HLSL query footprint against an
        // authoritative base-cell scan over multiple deterministic patterns.
        uint32_t randomState = 0x51f15e7du;
        for (uint32_t pattern = 0u; pattern < 16u; ++pattern)
        {
            for (uint32_t& depth : baseCells)
            {
                randomState =
                    randomState * 1664525u + 1013904223u;
                depth =
                    0x3e000000u | (randomState & 0x00ffffffu);
            }
            const SvsmStaticDepthHierarchyWords randomHierarchy =
                BuildSvsmStaticDepthHierarchyFromBaseCells(
                    baseCells, tag);
            for (uint32_t minimumY = 0u;
                minimumY < SvsmStaticDepthHierarchyBaseAxis;
                ++minimumY)
            {
                for (uint32_t minimumX = 0u;
                    minimumX < SvsmStaticDepthHierarchyBaseAxis;
                    ++minimumX)
                {
                    for (uint32_t maximumY = minimumY;
                        maximumY <
                            SvsmStaticDepthHierarchyBaseAxis;
                        ++maximumY)
                    {
                        for (uint32_t maximumX = minimumX;
                            maximumX <
                                SvsmStaticDepthHierarchyBaseAxis;
                            ++maximumX)
                        {
                            assert(
                                QuerySvsmStaticDepthHierarchyRegion(
                                    randomHierarchy,
                                    minimumX,
                                    minimumY,
                                    maximumX,
                                    maximumY) ==
                                QuerySvsmStaticDepthHierarchyBaseCellRegion(
                                    randomHierarchy,
                                    minimumX,
                                    minimumY,
                                    maximumX,
                                    maximumY));
                        }
                    }
                }
            }
        }

        const SvsmPacketTexelRectangle texels =
            GetSvsmPacketTexelRectangle(
                128.25f, 255.75f, 260.25f, 384.5f);
        assert(UnpackSvsmPacketTexelCoordinateX(
            texels.packedMinimum) == 127u);
        assert(UnpackSvsmPacketTexelCoordinateY(
            texels.packedMinimum) == 254u);
        assert(UnpackSvsmPacketTexelCoordinateX(
            texels.packedMaximum) == 261u);
        assert(UnpackSvsmPacketTexelCoordinateY(
            texels.packedMaximum) == 385u);
        const SvsmPacketTexelRectangle outside =
            GetSvsmPacketTexelRectangle(
                -10.f, -10.f, -2.f, -2.f);
        assert(outside.packedMinimum == SvsmEmptyPacketPageBounds);
        assert(outside.packedMaximum == SvsmEmptyPacketPageBounds);

        constexpr float casterDepth = 0.4f;
        constexpr float staticDepth = 0.5f;
        constexpr float bias = 0.01f;
        const auto shouldCull = [](
            bool enabled,
            bool paired,
            bool scatter,
            bool staticCaster,
            bool boundsReliable,
            bool resident,
            bool dirty,
            bool staticDirty,
            bool ownerMatches,
            bool scheduledOwnerMatches,
            bool tagMatches,
            float caster,
            float occluder,
            float conservativeBias) {
                return ShouldCullSvsmDynamicCasterAgainstStaticDepth(
                    enabled,
                    paired,
                    scatter,
                    staticCaster,
                    boundsReliable,
                    resident,
                    dirty,
                    staticDirty,
                    ownerMatches,
                    scheduledOwnerMatches,
                    tagMatches,
                    caster,
                    occluder,
                    conservativeBias);
            };
        assert(shouldCull(
            true, true, false, false, true,
            true, true, false, true, true, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            false, true, false, false, true,
            true, true, false, true, true, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, false, false, false, true,
            true, true, false, true, true, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, true, true, false, true,
            true, true, false, true, true, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, true, false, true, true,
            true, true, false, true, true, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, true, false, false, false,
            true, true, false, true, true, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            false, true, false, true, true, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, false, false, true, true, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, true, true, true, true, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, true, false, false, true, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, true, false, true, false, true,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, true, false, true, true, false,
            casterDepth, staticDepth, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, true, false, true, true, true,
            casterDepth, 0.f, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, true, false, true, true, true,
            casterDepth, casterDepth + bias, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, true, false, true, true, true,
            std::numeric_limits<float>::quiet_NaN(),
            staticDepth, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, true, false, true, true, true,
            std::nextafter(1.f, 2.f), staticDepth, bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, true, false, true, true, true,
            casterDepth, std::nextafter(1.f, 2.f), bias));
        assert(!shouldCull(
            true, true, false, false, true,
            true, true, false, true, true, true,
            casterDepth, staticDepth, 0.0501f));

        // The hierarchy resource owns invalidation independently from the
        // query toggle. Simulate disabling queries, changing static content,
        // and re-enabling: the old same-owner tag must already be zero.
        uint32_t retainedTag = tag;
        const bool queryToggleEnabled = false;
        (void)queryToggleEnabled;
        if (ShouldInvalidateSvsmStaticDepthHierarchyTag(
                true, true, true))
        {
            retainedTag = 0u;
        }
        assert(retainedTag == 0u);
        assert(!IsSvsmStaticDepthHierarchyTagValid(
            retainedTag, owner));
        assert(!ShouldInvalidateSvsmStaticDepthHierarchyTag(
            false, true, true));
        assert(!ShouldInvalidateSvsmStaticDepthHierarchyTag(
            true, false, true));
        assert(!ShouldInvalidateSvsmStaticDepthHierarchyTag(
            true, true, false));

        // Cold-off warmup must not let the static zero-work path bypass the
        // first later enable. Bootstrap reads the already-complete paired
        // static slices and does not require a shadow-depth redraw.
        bool bootstrapRequired = true;
        assert(!ShouldBootstrapSvsmStaticDepthHierarchy(
            bootstrapRequired, false));
        bootstrapRequired =
            GetNextSvsmStaticDepthHierarchyBootstrapRequired(
                bootstrapRequired, false, false);
        assert(bootstrapRequired);
        assert(ShouldBootstrapSvsmStaticDepthHierarchy(
            bootstrapRequired, true));
        assert(
            GetNextSvsmStaticDepthHierarchyBootstrapRequired(
                bootstrapRequired, true, false));
        bootstrapRequired =
            GetNextSvsmStaticDepthHierarchyBootstrapRequired(
                bootstrapRequired, true, true);
        assert(!bootstrapRequired);
        assert(!ShouldBootstrapSvsmStaticDepthHierarchy(
            bootstrapRequired, true));
        // Any effective-disabled interval rearms the one-time scan because
        // static tag invalidations can occur without hierarchy rebuilds.
        bootstrapRequired =
            GetNextSvsmStaticDepthHierarchyBootstrapRequired(
                bootstrapRequired, false, false);
        assert(bootstrapRequired);

        assert(ShouldUseSvsmStaticDepthHierarchyCulling(
            SvsmMode::SparseCached,
            true, true, true, false, true));
        assert(!ShouldUseSvsmStaticDepthHierarchyCulling(
            SvsmMode::DenseReference,
            true, true, true, false, true));
        assert(!ShouldUseSvsmStaticDepthHierarchyCulling(
            SvsmMode::SparseCached,
            true, true, true, true, true));
        assert(!ShouldUseSvsmStaticDepthHierarchyCulling(
            SvsmMode::SparseCached,
            true, true, true, false, false));
    }

    void TestReceiverPageMaskHierarchy()
    {
        static_assert(SvsmReceiverPageMaskCellWidth == 16u);
        static_assert(SvsmReceiverPageMaskAxis == 8u);
        static_assert(SvsmReceiverPageMaskQuadrantAxis == 2u);
        static_assert(
            SvsmReceiverPageMaskQuadrantCellAxis == 4u);
        static_assert(SvsmReceiverPageMaskTagOffset == 0u);
        static_assert(SvsmReceiverPageMaskQuadrantOffset == 1u);
        static_assert(SvsmReceiverPageMaskQuadrantCount == 4u);
        static_assert(SvsmReceiverPageMaskWordsPerPage == 5u);
        static_assert(SvsmReceiverPageMaskQueryCounter == 45u);
        static_assert(SvsmReceiverPageMaskCullCounter == 46u);
        static_assert(SvsmReceiverPageMaskFailOpenCounter == 47u);
        static_assert(
            SvsmReceiverPageMaskFailOpenCounter + 1u ==
            SvsmCounterCount);

        constexpr uint32_t FullLevelMask =
            (1u << SvsmClipmapCount) - 1u;
        for (uint32_t selectedLevel = 0u;
            selectedLevel <= SvsmClipmapCount;
            ++selectedLevel)
        {
            const uint32_t requestedLevels =
                GetSvsmPerPixelReceiverRequestedLevelMask(
                    selectedLevel);
            const uint32_t maskOnlyLevels =
                GetSvsmPerPixelReceiverMaskOnlyFallbackLevelMask(
                    selectedLevel);
            uint32_t expectedCoverage = 0u;
            for (uint32_t level = selectedLevel;
                level < SvsmClipmapCount;
                ++level)
            {
                expectedCoverage |= 1u << level;
            }
            // Mask-only fallback coverage must never create a page request,
            // while its union with the intended finest/coarsest requests
            // covers every intermediate level that resolve may select.
            assert((requestedLevels & maskOnlyLevels) == 0u);
            assert((requestedLevels | maskOnlyLevels) ==
                expectedCoverage);
        }
        assert(GetSvsmPerPixelReceiverRequestedLevelMask(1u) ==
            ((1u << 1u) | (1u << 5u)));
        assert(
            GetSvsmPerPixelReceiverMaskOnlyFallbackLevelMask(1u) ==
            ((1u << 2u) | (1u << 3u) | (1u << 4u)));

        // Exhaust all geometric-coverage and residency patterns. Any level
        // accepted by resolve must have receiver-mask coverage, including an
        // intermediate page that was requested by a different receiver.
        for (uint32_t firstLevel = 0u;
            firstLevel <= SvsmClipmapCount;
            ++firstLevel)
        {
            for (uint32_t geometricBits = 0u;
                geometricBits <= FullLevelMask;
                ++geometricBits)
            {
                std::array<bool, SvsmClipmapCount>
                    geometricallyCoveredLevels = {};
                for (uint32_t level = 0u;
                    level < SvsmClipmapCount;
                    ++level)
                {
                    geometricallyCoveredLevels[level] =
                        (geometricBits & (1u << level)) != 0u;
                }
                const uint32_t fallbackCoverage =
                    BuildSvsmReceiverFallbackCoverageLevelMask(
                        firstLevel,
                        geometricallyCoveredLevels);
                const uint32_t levelsBeforeFirst =
                    firstLevel == 0u
                    ? 0u
                    : ((1u << firstLevel) - 1u);
                assert(fallbackCoverage ==
                    (geometricBits & FullLevelMask &
                        ~levelsBeforeFirst));

                for (uint32_t residentBits = 0u;
                    residentBits <= FullLevelMask;
                    ++residentBits)
                {
                    std::array<bool, SvsmClipmapCount>
                        validLevels = {};
                    for (uint32_t level = 0u;
                        level < SvsmClipmapCount;
                        ++level)
                    {
                        validLevels[level] =
                            geometricallyCoveredLevels[level] &&
                            (residentBits & (1u << level)) != 0u;
                    }
                    const uint32_t resolvedLevel =
                        SelectSvsmFallbackLevel(
                            firstLevel, validLevels);
                    if (resolvedLevel < SvsmClipmapCount)
                    {
                        assert((fallbackCoverage &
                            (1u << resolvedLevel)) != 0u);
                    }
                }
            }
        }

        assert(GetSvsmReceiverPageMaskWordBase(0u) == 0u);
        assert(GetSvsmReceiverPageMaskWordBase(1u) == 5u);
        assert(GetSvsmReceiverPageMaskWordBase(
            SvsmClipmapCount * SvsmPagesPerClipmap - 1u) ==
            (SvsmClipmapCount * SvsmPagesPerClipmap - 1u) * 5u);
        assert(BuildSvsmReceiverPageMaskQuadrantRectangle(
            0u, 0u, 0u, 0u) == 0x0001u);
        assert(BuildSvsmReceiverPageMaskQuadrantRectangle(
            3u, 3u, 3u, 3u) == 0x8000u);
        assert(BuildSvsmReceiverPageMaskQuadrantRectangle(
            0u, 0u, 3u, 3u) == 0xffffu);
        assert(BuildSvsmReceiverPageMaskQuadrantRectangle(
            1u, 1u, 2u, 2u) == 0x0660u);
        assert(BuildSvsmReceiverPageMaskQuadrantRectangle(
            3u, 0u, 2u, 0u) == 0u);
        assert(BuildSvsmReceiverPageMaskQuadrantRectangle(
            0u, 0u, 4u, 0u) == 0u);

        constexpr uint32_t Generation = 29u;
        SvsmReceiverPageMask mask;
        mask = AddSvsmReceiverPageMaskRectangle(
            mask, Generation, 3u, 3u, 4u, 4u);
        assert(mask.generation == Generation);
        assert(mask.quadrants[0u] == 0x8000u);
        assert(mask.quadrants[1u] == 0x1000u);
        assert(mask.quadrants[2u] == 0x0008u);
        assert(mask.quadrants[3u] == 0x0001u);
        assert(QuerySvsmReceiverPageMask(
            mask, Generation, 3u, 3u, 4u, 4u) ==
            SvsmReceiverPageMaskQuery::Positive);
        assert(QuerySvsmReceiverPageMask(
            mask, Generation, 0u, 0u, 2u, 2u) ==
            SvsmReceiverPageMaskQuery::Reject);
        assert(QuerySvsmReceiverPageMask(
            mask, Generation + 1u, 3u, 3u, 4u, 4u) ==
            SvsmReceiverPageMaskQuery::FailOpen);
        assert(QuerySvsmReceiverPageMask(
            mask, 0u, 3u, 3u, 4u, 4u) ==
            SvsmReceiverPageMaskQuery::FailOpen);
        assert(QuerySvsmReceiverPageMask(
            mask, Generation, 4u, 4u, 3u, 4u) ==
            SvsmReceiverPageMaskQuery::FailOpen);
        assert(QuerySvsmReceiverPageMask(
            mask, Generation, 0u, 0u, 8u, 0u) ==
            SvsmReceiverPageMaskQuery::FailOpen);

        SvsmReceiverPageMask corrupt = mask;
        corrupt.quadrants[2u] |= 1u << 24u;
        assert(QuerySvsmReceiverPageMask(
            corrupt, Generation, 3u, 3u, 4u, 4u) ==
            SvsmReceiverPageMaskQuery::FailOpen);
        SvsmReceiverPageMask invalidAdd = mask;
        invalidAdd = AddSvsmReceiverPageMaskRectangle(
            invalidAdd, 0u, 0u, 0u, 0u, 0u);
        assert(invalidAdd.generation == 0u);
        assert(QuerySvsmReceiverPageMask(
            invalidAdd, Generation, 0u, 0u, 7u, 7u) ==
            SvsmReceiverPageMaskQuery::FailOpen);

        // Exhaust every rectangle crossing the 4x4 quadrant boundaries.
        // A singleton query must agree with the exact rectangle coverage.
        for (uint32_t minimumY = 0u;
            minimumY < SvsmReceiverPageMaskAxis;
            ++minimumY)
        {
            for (uint32_t minimumX = 0u;
                minimumX < SvsmReceiverPageMaskAxis;
                ++minimumX)
            {
                for (uint32_t maximumY = minimumY;
                    maximumY < SvsmReceiverPageMaskAxis;
                    ++maximumY)
                {
                    for (uint32_t maximumX = minimumX;
                        maximumX < SvsmReceiverPageMaskAxis;
                        ++maximumX)
                    {
                        const SvsmReceiverPageMask rectangle =
                            AddSvsmReceiverPageMaskRectangle(
                                {},
                                Generation,
                                minimumX,
                                minimumY,
                                maximumX,
                                maximumY);
                        for (uint32_t queryY = 0u;
                            queryY < SvsmReceiverPageMaskAxis;
                            ++queryY)
                        {
                            for (uint32_t queryX = 0u;
                                queryX < SvsmReceiverPageMaskAxis;
                                ++queryX)
                            {
                                const bool covered =
                                    queryX >= minimumX &&
                                    queryX <= maximumX &&
                                    queryY >= minimumY &&
                                    queryY <= maximumY;
                                assert(QuerySvsmReceiverPageMask(
                                    rectangle,
                                    Generation,
                                    queryX,
                                    queryY,
                                    queryX,
                                    queryY) ==
                                    (covered
                                        ? SvsmReceiverPageMaskQuery::Positive
                                        : SvsmReceiverPageMaskQuery::Reject));
                            }
                        }
                    }
                }
            }
        }

        // Build a deterministic sparse union, then exhaust every query
        // rectangle against a brute-force 8x8 occupancy model.
        std::array<bool, 64u> occupied{};
        SvsmReceiverPageMask unionMask;
        constexpr std::array<std::array<uint32_t, 4u>, 5u>
            Rectangles = {{
                {{ 0u, 0u, 0u, 7u }},
                {{ 2u, 1u, 5u, 2u }},
                {{ 7u, 0u, 7u, 0u }},
                {{ 3u, 3u, 4u, 4u }},
                {{ 6u, 6u, 7u, 7u }}
            }};
        for (const auto& rectangle : Rectangles)
        {
            unionMask = AddSvsmReceiverPageMaskRectangle(
                unionMask,
                Generation,
                rectangle[0u],
                rectangle[1u],
                rectangle[2u],
                rectangle[3u]);
            for (uint32_t y = rectangle[1u];
                y <= rectangle[3u];
                ++y)
            {
                for (uint32_t x = rectangle[0u];
                    x <= rectangle[2u];
                    ++x)
                {
                    occupied[y * SvsmReceiverPageMaskAxis + x] =
                        true;
                }
            }
        }
        for (uint32_t minimumY = 0u;
            minimumY < SvsmReceiverPageMaskAxis;
            ++minimumY)
        {
            for (uint32_t minimumX = 0u;
                minimumX < SvsmReceiverPageMaskAxis;
                ++minimumX)
            {
                for (uint32_t maximumY = minimumY;
                    maximumY < SvsmReceiverPageMaskAxis;
                    ++maximumY)
                {
                    for (uint32_t maximumX = minimumX;
                        maximumX < SvsmReceiverPageMaskAxis;
                        ++maximumX)
                    {
                        bool expectedPositive = false;
                        for (uint32_t y = minimumY;
                            y <= maximumY;
                            ++y)
                        {
                            for (uint32_t x = minimumX;
                                x <= maximumX;
                                ++x)
                            {
                                expectedPositive |= occupied[
                                    y * SvsmReceiverPageMaskAxis + x];
                            }
                        }
                        assert(QuerySvsmReceiverPageMask(
                            unionMask,
                            Generation,
                            minimumX,
                            minimumY,
                            maximumX,
                            maximumY) ==
                            (expectedPositive
                                ? SvsmReceiverPageMaskQuery::Positive
                                : SvsmReceiverPageMaskQuery::Reject));
                    }
                }
            }
        }

        for (uint32_t maskBits = 0u;
            maskBits < 256u;
            ++maskBits)
        {
            const bool enabled = (maskBits & 1u) != 0u;
            const bool packet = (maskBits & 2u) != 0u;
            const bool cache = (maskBits & 4u) != 0u;
            const bool paired = (maskBits & 8u) != 0u;
            const bool scatter = (maskBits & 16u) != 0u;
            const bool rebuilt = (maskBits & 32u) != 0u;
            const bool resources = (maskBits & 64u) != 0u;
            const bool generation = (maskBits & 128u) != 0u;
            const bool expected =
                enabled && packet && !cache && !paired &&
                rebuilt && resources && generation;
            assert(ShouldUseSvsmReceiverPageMaskCulling(
                SvsmMode::SparseCached,
                enabled,
                packet,
                cache,
                paired,
                scatter,
                rebuilt,
                resources,
                generation ? Generation : 0u) == expected);
            assert(ShouldUseSvsmReceiverPageMaskCulling(
                SvsmMode::SparseUncached,
                enabled,
                packet,
                cache,
                paired,
                scatter,
                rebuilt,
                resources,
                generation ? Generation : 0u) == expected);
            assert(!ShouldUseSvsmReceiverPageMaskCulling(
                SvsmMode::DenseReference,
                enabled,
                packet,
                cache,
                paired,
                scatter,
                rebuilt,
                resources,
                generation ? Generation : 0u));
        }
    }

    void TestStaticPageRequestActions()
    {
        assert(!IsSvsmStaticJitterActive(0.f, 0.f));
        assert(IsSvsmStaticJitterActive(0.25f, 0.f));
        assert(IsSvsmStaticJitterActive(0.f, -0.25f));
        assert(!ShouldResetSvsmStaticJitterCache(
            false, false, 0.f, 0.f));

        bool previousJitterActive = false;
        for (uint64_t phase = 0u;
            phase < MiniEngineTaaHalton23.size();
            ++phase)
        {
            const MiniEngineTaaJitterSample offset =
                GetMiniEngineTaaJitter(phase);
            assert(IsSvsmStaticJitterActive(offset.x, offset.y));
            assert(ShouldResetSvsmStaticJitterCache(
                true,
                previousJitterActive,
                offset.x,
                offset.y) == (phase == 0u));
            previousJitterActive = true;
        }
        assert(ShouldResetSvsmStaticJitterCache(
            true, previousJitterActive, 0.f, 0.f));
        previousJitterActive = false;
        assert(!ShouldResetSvsmStaticJitterCache(
            true, previousJitterActive, 0.f, 0.f));

        assert(SelectSvsmStaticPageRequestAction(false, false) ==
            SvsmStaticPageRequestAction::Rebuild);
        assert(SelectSvsmStaticPageRequestAction(false, true) ==
            SvsmStaticPageRequestAction::Rebuild);
        assert(SelectSvsmStaticPageRequestAction(true, false) ==
            SvsmStaticPageRequestAction::ExtendUnion);
        assert(SelectSvsmStaticPageRequestAction(true, true) ==
            SvsmStaticPageRequestAction::Reuse);
        assert(SelectSvsmStaticPageRequestAction(
            true, true, true) ==
            SvsmStaticPageRequestAction::Drain);
        const SvsmStaticPageRequestAction receiverDepthRefresh =
            SelectSvsmStaticPageRequestAction(
                true,
                true,
                true,
                true);
        assert(receiverDepthRefresh ==
            SvsmStaticPageRequestAction::ExtendUnion);
        assert(ShouldMarkSvsmStaticPageRequests(
            receiverDepthRefresh));
        assert(ShouldInvalidateSvsmStaticVisibility(
            receiverDepthRefresh));
        assert(ShouldInvalidateSvsmStaticVisibility(
            SvsmStaticPageRequestAction::Rebuild));
        assert(ShouldInvalidateSvsmStaticVisibility(
            SvsmStaticPageRequestAction::ExtendUnion));
        assert(ShouldInvalidateSvsmStaticVisibility(
            SvsmStaticPageRequestAction::Drain));
        assert(!ShouldInvalidateSvsmStaticVisibility(
            SvsmStaticPageRequestAction::Reuse));
        assert(ShouldMarkSvsmStaticPageRequests(
            SvsmStaticPageRequestAction::Rebuild));
        assert(ShouldMarkSvsmStaticPageRequests(
            SvsmStaticPageRequestAction::ExtendUnion));
        assert(!ShouldMarkSvsmStaticPageRequests(
            SvsmStaticPageRequestAction::Drain));
        assert(!ShouldMarkSvsmStaticPageRequests(
            SvsmStaticPageRequestAction::Reuse));
        assert(ShouldMaintainSvsmStaticPages(
            SvsmStaticPageRequestAction::Rebuild));
        assert(ShouldMaintainSvsmStaticPages(
            SvsmStaticPageRequestAction::ExtendUnion));
        assert(ShouldMaintainSvsmStaticPages(
            SvsmStaticPageRequestAction::Drain));
        assert(!ShouldMaintainSvsmStaticPages(
            SvsmStaticPageRequestAction::Reuse));
        assert(IsSvsmStaticPageMaintenanceOptimizationActive(
            true, SvsmStaticPageRequestAction::Rebuild));
        assert(IsSvsmStaticPageMaintenanceOptimizationActive(
            true, SvsmStaticPageRequestAction::Drain));
        assert(!IsSvsmStaticPageMaintenanceOptimizationActive(
            false, SvsmStaticPageRequestAction::Rebuild));
        assert(!IsSvsmStaticPageMaintenanceOptimizationActive(
            true, SvsmStaticPageRequestAction::Reuse));
        assert(CanReuseSvsmStaticVisibility(
            true, true, true, true));
        assert(!CanReuseSvsmStaticVisibility(
            false, true, true, true));
        assert(!CanReuseSvsmStaticVisibility(
            true, true, false, true));
        assert(!CanReuseSvsmStaticVisibility(
            true, true, true, false));
        assert(IsSvsmStaticVisibilityConfigurationCompatible(
            true,
            SvsmFilterMode::ManualPageSafe,
            SvsmFilterKernel::NearestPoisson,
            SvsmPoissonOrdering::LegacyStride,
            SvsmTapCount::Eight,
            SvsmResolutionBias::Zero,
            true,
            false,
            SvsmFilterMode::ManualPageSafe,
            SvsmFilterKernel::NearestPoisson,
            SvsmPoissonOrdering::LegacyStride,
            SvsmTapCount::Eight,
            SvsmResolutionBias::Zero,
            true,
            false));
        assert(!IsSvsmStaticVisibilityConfigurationCompatible(
            true,
            SvsmFilterMode::ManualPageSafe,
            SvsmFilterKernel::NearestPoisson,
            SvsmPoissonOrdering::LegacyStride,
            SvsmTapCount::Eight,
            SvsmResolutionBias::Zero,
            true,
            false,
            SvsmFilterMode::ManualPageSafe,
            SvsmFilterKernel::BilinearPcf,
            SvsmPoissonOrdering::LegacyStride,
            SvsmTapCount::Eight,
            SvsmResolutionBias::Zero,
            true,
            false));
        assert(!IsSvsmStaticVisibilityConfigurationCompatible(
            true,
            SvsmFilterMode::ManualPageSafe,
            SvsmFilterKernel::NearestPoisson,
            SvsmPoissonOrdering::LegacyStride,
            SvsmTapCount::Eight,
            SvsmResolutionBias::Zero,
            true,
            false,
            SvsmFilterMode::ManualPageSafe,
            SvsmFilterKernel::NearestPoisson,
            SvsmPoissonOrdering::BalancedProgressive,
            SvsmTapCount::Eight,
            SvsmResolutionBias::Zero,
            true,
            false));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, 1u,
            std::numeric_limits<uint32_t>::max(), true));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u,
            std::numeric_limits<uint32_t>::max(), true));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, SvsmPagesPerClipmap,
            std::numeric_limits<uint32_t>::max(), true));
        assert(!CanUseSvsmStaticPageRequestConfiguration(
            true, true, 0u,
            std::numeric_limits<uint32_t>::max(), true));
        assert(!CanUseSvsmStaticPageRequestConfiguration(
            true, true, SvsmPagesPerClipmap + 1u,
            std::numeric_limits<uint32_t>::max(), true));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u, 64u, true));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u, 65u, true));
        assert(!CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u, 63u, true));
        assert(CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u, 63u, true, true));
        assert(!CanUseSvsmStaticPageRequestConfiguration(
            true, true, 64u, 0u, true, true));
        assert(GetSvsmStaticPageDrainPassCount(0u, 4u) == 0u);
        assert(GetSvsmStaticPageDrainPassCount(4096u, 0u) == 0u);
        assert(GetSvsmStaticPageDrainPassCount(4096u, 4u) == 1024u);
        assert(GetSvsmStaticPageDrainPassCount(4096u, 256u) == 16u);
        assert(GetSvsmStaticPageDrainPassCount(4096u, 4096u) == 1u);
        assert(GetSvsmStaticPageDrainPassCount(4095u, 4096u) == 1u);
        assert(GetSvsmStaticPageDrainPassCount(
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max()) == 1u);
        assert(GetSvsmStaticPageDrainPassCount(
            std::numeric_limits<uint32_t>::max(), 1u) ==
            std::numeric_limits<uint32_t>::max());
        assert(CanUseSvsmStaticZeroWorkFastPath(
            true, true, false, false, false));
        assert(!CanUseSvsmStaticZeroWorkFastPath(
            false, true, false, false, false));
        assert(!CanUseSvsmStaticZeroWorkFastPath(
            true, false, false, false, false));
        const SvsmStaticPageRequestAction reusedRequestsWithLiveResolve =
            SelectSvsmStaticPageRequestAction(true, true, false);
        assert(reusedRequestsWithLiveResolve ==
            SvsmStaticPageRequestAction::Reuse);
        assert(!IsSvsmStaticPageMaintenanceOptimizationActive(
            true, reusedRequestsWithLiveResolve));
        assert(!CanUseSvsmStaticZeroWorkFastPath(
            true, true, true, false, false));
        assert(!CanUseSvsmStaticZeroWorkFastPath(
            true, true, false, true, false));
        assert(!CanUseSvsmStaticZeroWorkFastPath(
            true, true, false, false, true));

        // One fixed no-jitter phase marks once and then reuses. An eight-phase
        // cycle extends only for each unseen phase and performs no page work
        // throughout the next complete cycle.
        bool noJitterSeen = false;
        assert(SelectSvsmStaticPageRequestAction(
            true, noJitterSeen) ==
            SvsmStaticPageRequestAction::ExtendUnion);
        noJitterSeen = true;
        assert(SelectSvsmStaticPageRequestAction(
            true, noJitterSeen) ==
            SvsmStaticPageRequestAction::Reuse);

        std::array<bool, 8> jitterSeen{};
        for (uint32_t phase = 0u; phase < jitterSeen.size(); ++phase)
        {
            assert(SelectSvsmStaticPageRequestAction(
                true, jitterSeen[phase]) ==
                SvsmStaticPageRequestAction::ExtendUnion);
            jitterSeen[phase] = true;
        }
        for (uint32_t phase = 0u; phase < jitterSeen.size(); ++phase)
        {
            assert(SelectSvsmStaticPageRequestAction(
                true, jitterSeen[phase]) ==
                SvsmStaticPageRequestAction::Reuse);
        }

        uint32_t drainFramesRemaining =
            GetSvsmStaticPageDrainPassCount(4096u, 4u);
        for (uint32_t pass = 0u;
            pass < 1024u;
            ++pass)
        {
            const SvsmStaticPageRequestAction action =
                pass == 0u
                    ? SelectSvsmStaticPageRequestAction(
                        false, false, true)
                    : SelectSvsmStaticPageRequestAction(
                        true, true, drainFramesRemaining > 0u);
            assert(action == (pass == 0u
                ? SvsmStaticPageRequestAction::Rebuild
                : SvsmStaticPageRequestAction::Drain));
            assert(ShouldMaintainSvsmStaticPages(action));
            assert(ShouldMarkSvsmStaticPageRequests(action) ==
                (pass == 0u));
            --drainFramesRemaining;
        }
        assert(drainFramesRemaining == 0u);
        assert(SelectSvsmStaticPageRequestAction(
            true, true, drainFramesRemaining > 0u) ==
            SvsmStaticPageRequestAction::Reuse);
    }

    void TestClipmapSelection()
    {
        assert(GetSvsmFirstClipmapLevel(
            SvsmResolutionBias::Zero) == 0u);
        assert(GetSvsmFirstClipmapLevel(
            SvsmResolutionBias::PlusOne) == 1u);
        assert(GetSvsmFirstClipmapLevel(
            SvsmResolutionBias::PlusTwo) == 2u);
        assert(SelectFinestSvsmClipmap(
            0.f, 0.f, 20.f, SvsmResolutionBias::Zero) == 0);
        assert(SelectFinestSvsmClipmap(
            10.f, -10.f, 20.f, SvsmResolutionBias::Zero) == 0);
        assert(SelectFinestSvsmClipmap(
            10.01f, 0.f, 20.f, SvsmResolutionBias::Zero) == 1);
        assert(SelectFinestSvsmClipmap(
            160.01f, 0.f, 20.f, SvsmResolutionBias::Zero) == 5);
        assert(SelectFinestSvsmClipmap(
            321.f, 0.f, 20.f, SvsmResolutionBias::Zero) == -1);
        assert(SelectFinestSvsmClipmap(
            0.f, 0.f, 20.f, SvsmResolutionBias::PlusTwo) == 2);
        assert(SelectFinestSvsmClipmap(
            std::numeric_limits<float>::quiet_NaN(),
            0.f,
            20.f,
            SvsmResolutionBias::Zero) == -1);
    }

    void TestReverseDepthWrites()
    {
        uint32_t depth = 0u;
        depth = WriteSvsmReverseDepth(depth, 0.1f);
        assert(std::abs(DecodeSvsmReverseDepth(depth) - 0.1f) < 1e-7f);
        depth = WriteSvsmReverseDepth(depth, 0.9f);
        assert(std::abs(DecodeSvsmReverseDepth(depth) - 0.9f) < 1e-7f);
        depth = WriteSvsmReverseDepth(depth, 0.4f);
        assert(std::abs(DecodeSvsmReverseDepth(depth) - 0.9f) < 1e-7f);

        assert(EncodeSvsmReverseDepth(-1.f) == 0u);
        assert(DecodeSvsmReverseDepth(
            EncodeSvsmReverseDepth(2.f)) == 1.f);
        assert(EncodeSvsmReverseDepth(
            std::numeric_limits<float>::quiet_NaN()) == 0u);
    }

    void TestAllocationEvictionAndCacheReuse()
    {
        assert(GetSvsmPageAgeElapsed(10u, 3u) == 7u);
        assert(GetSvsmPageAgeElapsed(3u, SvsmPageAgeMask - 1u) == 5u);
        assert(IsSvsmPageInsideRecentEvictionGrace(10u, 3u));
        assert(!IsSvsmPageInsideRecentEvictionGrace(11u, 3u));
        assert(IsSvsmPageInsideRecentEvictionGrace(
            3u, SvsmPageAgeMask - 1u));
        assert(IsSvsmPageInsideRecentEvictionGrace(
            3u, SvsmPageAgeMask - 3u));
        assert(!IsSvsmPageInsideRecentEvictionGrace(
            3u, SvsmPageAgeMask - 4u));
        assert(ClassifySvsmCachedPage(false, 3u, 3u) ==
            SvsmEvictionCandidateList::UnrecentCached);
        assert(ClassifySvsmCachedPage(true, 10u, 3u) ==
            SvsmEvictionCandidateList::RecentCached);
        assert(ClassifySvsmCachedPage(true, 11u, 3u) ==
            SvsmEvictionCandidateList::UnrecentCached);

        const uint32_t coarsestLevel = SvsmClipmapCount - 1u;
        assert(SelectSvsmEvictionCandidateList(
            coarsestLevel, 1u, 1u, 1u, 1u) ==
            SvsmEvictionCandidateList::Free);
        assert(SelectSvsmEvictionCandidateList(
            coarsestLevel, 0u, 1u, 1u, 1u) ==
            SvsmEvictionCandidateList::UnrecentCached);
        assert(SelectSvsmEvictionCandidateList(
            coarsestLevel, 0u, 0u, 1u, 1u) ==
            SvsmEvictionCandidateList::RecentCached);
        assert(SelectSvsmEvictionCandidateList(
            coarsestLevel, 0u, 0u, 0u, 1u) ==
            SvsmEvictionCandidateList::RequiredFine);
        assert(SelectSvsmEvictionCandidateList(
            coarsestLevel - 1u, 0u, 0u, 0u, 1u) ==
            SvsmEvictionCandidateList::None);

        struct Slot
        {
            uint32_t owner = SvsmInvalidPhysicalPage;
            uint32_t age = 0u;
            bool visited = false;
        };

        std::array<Slot, 2> pool{};
        pool[0] = { 10u, 5u, true };
        pool[1] = { 20u, 1u, false };

        auto allocate = [&pool](uint32_t owner) {
            for (uint32_t index = 0; index < pool.size(); ++index)
            {
                if (pool[index].owner == SvsmInvalidPhysicalPage)
                {
                    pool[index] = { owner, 0u, true };
                    return index;
                }
            }

            uint32_t victim = SvsmInvalidPhysicalPage;
            uint32_t oldestAge = 0u;
            for (uint32_t index = 0; index < pool.size(); ++index)
            {
                if (!pool[index].visited && pool[index].age >= oldestAge)
                {
                    victim = index;
                    oldestAge = pool[index].age;
                }
            }
            if (victim == SvsmInvalidPhysicalPage)
                return victim;
            pool[victim] = { owner, 0u, true };
            return victim;
        };

        pool[1].age = 9u;
        assert(allocate(30u) == 1u);
        assert(pool[0].owner == 10u);
        assert(pool[1].owner == 30u);

        // The same wrapped virtual page retains its owner and requires no new
        // render work while camera, light, and caster state remain unchanged.
        SvsmPageMetadata cached;
        cached.physicalPage = 0u;
        cached.resident = true;
        cached.required = true;
        cached.dirty = false;
        const uint32_t before = PackSvsmPageMetadata(cached);
        const uint32_t after = PackSvsmPageMetadata(cached);
        assert(before == after);
        assert(!UnpackSvsmPageMetadata(after).dirty);
    }

    void TestInvalidationAndFallback()
    {
        SvsmPageMetadata metadata;
        metadata.physicalPage = 42u;
        metadata.resident = true;
        metadata.required = true;
        metadata.dirty = false;

        metadata.dirty = true;
        assert(UnpackSvsmPageMetadata(
            PackSvsmPageMetadata(metadata)).dirty);

        std::array<bool, SvsmClipmapCount> valid{};
        valid[3] = true;
        valid[5] = true;
        assert(SelectSvsmFallbackLevel(1u, valid) == 3u);
        valid[3] = false;
        assert(SelectSvsmFallbackLevel(1u, valid) == 5u);
        valid[5] = false;
        assert(SelectSvsmFallbackLevel(1u, valid) == SvsmClipmapCount);
    }

    void TestPageBoundaryFiltering()
    {
        const auto nearlyEqual = [](float left, float right) {
            return std::abs(left - right) <= 1e-6f;
        };
        const SvsmBilinearFootprint texelCenter =
            GetSvsmBilinearFootprint(23.5f, 71.5f);
        assert(texelCenter.minimumX == 23);
        assert(texelCenter.minimumY == 71);
        assert(nearlyEqual(texelCenter.fractionX, 0.f));
        assert(nearlyEqual(texelCenter.fractionY, 0.f));

        const SvsmBilinearFootprint texelBoundary =
            GetSvsmBilinearFootprint(24.f, 72.f);
        assert(texelBoundary.minimumX == 23);
        assert(texelBoundary.minimumY == 71);
        assert(nearlyEqual(texelBoundary.fractionX, 0.5f));
        assert(nearlyEqual(texelBoundary.fractionY, 0.5f));

        const SvsmBilinearFootprint pageInteriorCenter =
            GetSvsmBilinearFootprint(127.5f, 64.5f);
        assert(pageInteriorCenter.minimumX == 127);
        assert(pageInteriorCenter.minimumY == 64);
        assert(nearlyEqual(pageInteriorCenter.fractionX, 0.f));
        assert(nearlyEqual(pageInteriorCenter.fractionY, 0.f));

        const SvsmBilinearFootprint pageBoundary =
            GetSvsmBilinearFootprint(128.f, 64.5f);
        assert(pageBoundary.minimumX == 127);
        assert(pageBoundary.minimumY == 64);
        assert(nearlyEqual(pageBoundary.fractionX, 0.5f));
        assert(nearlyEqual(pageBoundary.fractionY, 0.f));

        const SvsmBilinearFootprint quarterTexel =
            GetSvsmBilinearFootprint(128.25f, 64.75f);
        assert(quarterTexel.minimumX == 127);
        assert(quarterTexel.minimumY == 64);
        assert(nearlyEqual(quarterTexel.fractionX, 0.75f));
        assert(nearlyEqual(quarterTexel.fractionY, 0.25f));

        assert(GetSvsmFilterRadius(SvsmTapCount::One) == 0u);
        assert(GetSvsmFilterRadius(SvsmTapCount::Four) == 3u);
        assert(GetSvsmFilterRadius(SvsmTapCount::Eight) == 3u);
        assert(GetSvsmFilterRadius(SvsmTapCount::Sixteen) == 3u);
        assert(GetSvsmFilterRadius(
            SvsmTapCount::One,
            SvsmFilterKernel::NearestPoisson) == 0u);
        assert(GetSvsmFilterRadius(
            SvsmTapCount::One,
            SvsmFilterKernel::BilinearPcf) == 1u);
        for (SvsmTapCount tapCount : {
                SvsmTapCount::Four,
                SvsmTapCount::Eight,
                SvsmTapCount::Sixteen })
        {
            assert(GetSvsmFilterRadius(
                tapCount,
                SvsmFilterKernel::NearestPoisson) == 3u);
            assert(GetSvsmFilterRadius(
                tapCount,
                SvsmFilterKernel::BilinearPcf) == 4u);
        }
        assert(IsSvsmFilterFootprintInsidePage(
            0u,
            0u,
            GetSvsmFilterRadius(SvsmTapCount::One)));
        assert(!IsSvsmFilterFootprintInsidePage(
            0u,
            0u,
            GetSvsmFilterRadius(SvsmTapCount::Sixteen)));
        assert(IsSvsmFilterFootprintInsidePage(64u, 64u, 4u));
        assert(!IsSvsmFilterFootprintInsidePage(127u, 64u, 1u));
        assert(!IsSvsmFilterFootprintInsidePage(128u, 64u, 1u));
        assert(IsSvsmFilterFootprintInsidePage(129u, 65u, 1u));
        assert(IsSvsmFilterFootprintInsidePage(127u, 127u, 0u));
        assert(!IsSvsmFilterFootprintInsidePage(0u, 64u, 1u));
        assert(IsSvsmFilterFootprintInsidePage(1u, 64u, 1u));
        assert(IsSvsmFilterFootprintInsidePage(126u, 64u, 1u));
        assert(!IsSvsmFilterFootprintInsidePage(127u, 64u, 1u));
        assert(!IsSvsmFilterFootprintInsidePage(3u, 64u, 4u));
        assert(IsSvsmFilterFootprintInsidePage(4u, 64u, 4u));
        assert(IsSvsmFilterFootprintInsidePage(123u, 64u, 4u));
        assert(!IsSvsmFilterFootprintInsidePage(124u, 64u, 4u));

        // Adjacent virtual pages intentionally receive unrelated physical
        // owners; receiver translation must happen independently per tap.
        std::array<uint32_t, 2> neighboringPhysicalPages = { 91u, 7u };
        assert(neighboringPhysicalPages[1] !=
            neighboringPhysicalPages[0] + 1u);
    }

    void TestPoissonOrdering()
    {
        constexpr std::array<uint32_t, 16u> expectedBalanced = {
            1u, 5u, 12u, 13u,
            4u, 7u, 8u, 11u,
            0u, 2u, 3u, 6u,
            9u, 10u, 14u, 15u
        };
        assert(
            SvsmBalancedProgressivePoissonOrder == expectedBalanced);

        constexpr std::array<std::array<float, 2u>, 16u> poisson = {{
            {{ -0.3935238f, 0.7530643f }},
            {{ -0.3022015f, 0.2976640f }},
            {{ 0.09813362f, 0.1924510f }},
            {{ -0.7593753f, 0.5187950f }},
            {{ 0.2293134f, 0.7607011f }},
            {{ 0.6505286f, 0.6297367f }},
            {{ 0.5322764f, 0.2350069f }},
            {{ 0.8581018f, -0.01624052f }},
            {{ -0.6928226f, 0.07119545f }},
            {{ -0.3114384f, -0.3017288f }},
            {{ 0.2837671f, -0.1797430f }},
            {{ -0.3093514f, -0.7492560f }},
            {{ -0.7386893f, -0.5215692f }},
            {{ 0.3988827f, -0.6170120f }},
            {{ 0.8114883f, -0.4580260f }},
            {{ 0.08265103f, -0.8939569f }}
        }};

        auto checkBalancedPrefix = [&](uint32_t tapCount) {
            std::array<uint32_t, 4u> quadrants{};
            float centroidX = 0.f;
            float centroidY = 0.f;
            for (uint32_t tap = 0u; tap < tapCount; ++tap)
            {
                const uint32_t sample = GetSvsmPoissonSampleIndex(
                    SvsmPoissonOrdering::BalancedProgressive,
                    SvsmTapCount(tapCount),
                    tap);
                assert(sample == expectedBalanced[tap]);
                const float x = poisson[sample][0];
                const float y = poisson[sample][1];
                const uint32_t quadrant =
                    (x >= 0.f ? 1u : 0u) |
                    (y >= 0.f ? 2u : 0u);
                ++quadrants[quadrant];
                centroidX += x;
                centroidY += y;
            }
            centroidX /= float(tapCount);
            centroidY /= float(tapCount);
            const uint32_t expectedPerQuadrant = tapCount / 4u;
            for (uint32_t count : quadrants)
                assert(count == expectedPerQuadrant);
            if (tapCount == 4u)
            {
                assert(std::abs(centroidX - 0.0021304f) < 1e-5f);
                assert(std::abs(centroidY + 0.0527951f) < 1e-5f);
            }
            else
            {
                assert(std::abs(centroidX - 0.0117202f) < 1e-5f);
                assert(std::abs(centroidY + 0.0180976f) < 1e-5f);
            }
        };
        checkBalancedPrefix(4u);
        checkBalancedPrefix(8u);

        assert(GetSvsmPoissonSampleIndex(
            SvsmPoissonOrdering::LegacyStride,
            SvsmTapCount::One,
            0u) == 0u);
        assert(GetSvsmPoissonSampleIndex(
            SvsmPoissonOrdering::BalancedProgressive,
            SvsmTapCount::One,
            0u) == 0u);
        for (uint32_t tap = 0u; tap < 4u; ++tap)
        {
            assert(GetSvsmPoissonSampleIndex(
                SvsmPoissonOrdering::LegacyStride,
                SvsmTapCount::Four,
                tap) == tap * 4u);
        }
        for (uint32_t tap = 0u; tap < 8u; ++tap)
        {
            assert(GetSvsmPoissonSampleIndex(
                SvsmPoissonOrdering::LegacyStride,
                SvsmTapCount::Eight,
                tap) == tap * 2u);
        }
        for (uint32_t tap = 0u; tap < 16u; ++tap)
        {
            assert(GetSvsmPoissonSampleIndex(
                SvsmPoissonOrdering::LegacyStride,
                SvsmTapCount::Sixteen,
                tap) == tap);
            assert(GetSvsmPoissonSampleIndex(
                SvsmPoissonOrdering::BalancedProgressive,
                SvsmTapCount::Sixteen,
                tap) == expectedBalanced[tap]);
        }

        for (SvsmTapCount tapCount : {
                SvsmTapCount::Four,
                SvsmTapCount::Eight,
                SvsmTapCount::Sixteen })
        {
            assert(GetSvsmAdaptiveProbeCount(tapCount) == 4u);
            for (uint32_t probe = 0u; probe < 4u; ++probe)
            {
                assert(GetSvsmAdaptiveProbeTapOrdinal(probe) == probe);
                assert(GetSvsmAdaptiveProbeReuseIndex(
                    tapCount, probe) == probe);
            }
            assert(GetSvsmAdaptiveProbeReuseIndex(tapCount, 4u) ==
                std::numeric_limits<uint32_t>::max());
        }
        assert(GetSvsmAdaptiveProbeCount(SvsmTapCount::One) == 1u);

        std::array<bool, 64u> seenPermutations{};
        for (uint32_t ordering = 0u; ordering < 2u; ++ordering)
        {
            for (uint32_t kernel = 0u; kernel < 2u; ++kernel)
            {
                for (uint32_t transform = 0u; transform < 2u; ++transform)
                {
                    for (uint32_t translation = 0u;
                        translation < 2u;
                        ++translation)
                    {
                        for (SvsmTapCount tapCount : {
                                SvsmTapCount::One,
                                SvsmTapCount::Four,
                                SvsmTapCount::Eight,
                                SvsmTapCount::Sixteen })
                        {
                            const uint32_t permutation =
                                GetSvsmSparseResolvePermutationIndex(
                                    SvsmPoissonOrdering(ordering),
                                    kernel != 0u
                                        ? SvsmFilterKernel::BilinearPcf
                                        : SvsmFilterKernel::NearestPoisson,
                                    transform != 0u,
                                    translation != 0u,
                                    tapCount);
                            assert(permutation < seenPermutations.size());
                            assert(!seenPermutations[permutation]);
                            seenPermutations[permutation] = true;
                        }
                    }
                }
            }
        }
        for (bool seen : seenPermutations)
            assert(seen);
    }

    void TestProfiles()
    {
        const ReceiverTransformToken clipToWorld = { 3u };
        const ReceiverTransformToken worldToClip = { 7u };
        assert(BuildSvsmReceiverTransform(
                false, clipToWorld, worldToClip).value == 7u);
        assert(BuildSvsmReceiverTransform(
                true, clipToWorld, worldToClip).value == 37u);
        const auto referenceTransforms =
            BuildSvsmClipmapTransformPair(
                false, clipToWorld, worldToClip);
        assert(referenceTransforms.worldToClip.value == 7u);
        assert(referenceTransforms.receiverToClip.value == 7u);
        const auto precomposedTransforms =
            BuildSvsmClipmapTransformPair(
                true, clipToWorld, worldToClip);
        assert(precomposedTransforms.worldToClip.value == 7u);
        assert(precomposedTransforms.receiverToClip.value == 37u);

        SparseVirtualShadowMapSettings settings;
        settings.enabled = true;
        settings.firstClipmapExtent = 37.f;
        settings.maximumLightDepth = 480.f;
        settings.physicalPageCount = 2048u;
        assert(settings.preset == SvsmPreset::Quality);
        assert(settings.filterKernel == SvsmFilterKernel::BilinearPcf);
        assert(settings.poissonOrdering ==
            SvsmPoissonOrdering::BalancedProgressive);
        assert(settings.tapCount == SvsmTapCount::Eight);
        assert(settings.perPixelMarkingDedupeEnabled);
        assert(settings.lightDepthOriginGuardBandEnabled);
        assert(settings.lightDepthOriginGuardBandFraction == 0.9f);
        assert(!settings.coarsestPageRenderBudgetEnabled);
        assert(!settings.dirtyPageScatterRasterEnabled);
        assert(!settings.scatterAlphaTestEarlyRejectEnabled);
        assert(!settings.dirtyPageScatterAmplificationGuardEnabled);
        assert(settings.dirtyPageScatterMaximumAmplification == 4u);
        assert(settings.packetRectangleDirectScanEnabled);
        assert(settings.recentPageEvictionGraceEnabled);
        assert(settings.precomposedClipmapTransformsEnabled);
        assert(settings.staticVisibilityCachingEnabled);
        assert(settings.sharedClipmapPacketBuilderEnabled);
        assert(settings.persistentCasterSourceCachingEnabled);
        assert(settings.opaqueRasterSpecializationEnabled);
        assert(settings.leanAlphaTestedBindingsEnabled);
        assert(settings.pairedStaticDynamicDepthEnabled);
        assert(settings.deferredStaticDepthMergeEnabled);
        assert(settings.movingLightUncachedEnabled);
        assert(settings.
            retainPhysicalMappingsOnContentInvalidationEnabled);
        assert(settings.movingLightLodBiasEnabled);
        assert(settings.movingLightResolutionBias ==
            SvsmResolutionBias::Zero);
        assert(settings.movingLightLodRecoveryFrames ==
            SvsmDefaultMovingLightLodRecoveryFrames);
        assert(!settings.receiverDistanceMipClampEnabled);
        assert(settings.receiverDistanceMipClampStartScale == 1.5f);
        assert(settings.receiverDistanceMipClampMaximumLevel ==
            SvsmMaximumReceiverDistanceMipClampLevel);
        assert(settings.movingLightContinuousReceiverBiasEnabled);
        assert(settings.localizedInvalidationEnabled);
        assert(settings.tightLocalizedInvalidationBoundsEnabled);
        assert(settings.adaptiveCasterCacheClassificationEnabled);
        assert(settings.defaultObjectInvalidationMode ==
            SvsmObjectInvalidationMode::Auto);
        assert(settings.batchedDrawSubmissionEnabled);
        assert(settings.packetStateSortingEnabled);
        assert(settings.levelEmptyWorkSkipEnabled);
        assert(settings.packetPageCullingEnabled);
        assert(settings.hierarchicalScheduledPageMaskEnabled);
        assert(settings.receiverPageMaskCullingEnabled);
        assert(settings.staticDepthHierarchyCullingEnabled);
        assert(settings.staticDepthHierarchyBias == 0.0002f);
        assert(!settings.detailedGpuTimingEnabled);

        SparseVirtualShadowMapSettings customSettings = settings;
        customSettings.mode = SvsmMode::DenseReference;
        customSettings.poissonOrdering =
            SvsmPoissonOrdering::LegacyStride;
        customSettings.tapCount = SvsmTapCount::Four;
        customSettings.pageRenderBudget = 17u;
        customSettings.opaqueRasterSpecializationEnabled = false;
        customSettings.persistentCasterSourceCachingEnabled = false;
        customSettings.casterOnlySceneRevisionEnabled = false;
        customSettings.leanAlphaTestedBindingsEnabled = false;
        customSettings.deferredStaticDepthMergeEnabled = false;
        customSettings.lightDepthOriginGuardBandEnabled = false;
        customSettings.lightDepthOriginGuardBandFraction = 0.73f;
        customSettings.staticDepthHierarchyCullingEnabled = false;
        customSettings.staticDepthHierarchyBias = 0.003f;
        customSettings.tightLocalizedInvalidationBoundsEnabled = false;
        customSettings.receiverDistanceMipClampEnabled = true;
        customSettings.receiverDistanceMipClampStartScale = 2.25f;
        customSettings.receiverDistanceMipClampMaximumLevel = 2u;
        customSettings.movingLightContinuousReceiverBiasEnabled = false;
        SparseVirtualShadowMapSettings expectedCustom = customSettings;
        expectedCustom.preset = SvsmPreset::Custom;
        ApplySvsmPreset(customSettings, SvsmPreset::Custom);
        assert(IsSameSvsmConfiguration(
            customSettings, expectedCustom));
        SparseVirtualShadowMapSettings changedGuardBand =
            customSettings;
        changedGuardBand.lightDepthOriginGuardBandEnabled =
            !changedGuardBand.lightDepthOriginGuardBandEnabled;
        assert(!IsSameSvsmConfiguration(
            customSettings, changedGuardBand));
        changedGuardBand = customSettings;
        changedGuardBand.lightDepthOriginGuardBandFraction = 0.74f;
        assert(!IsSameSvsmConfiguration(
            customSettings, changedGuardBand));

        settings.dirtyPageScatterRasterEnabled = true;
        settings.scatterAlphaTestEarlyRejectEnabled = true;
        settings.dirtyPageScatterAmplificationGuardEnabled = true;
        settings.coarsestPageRenderBudgetEnabled = true;
        ApplySvsmPreset(settings, SvsmPreset::Performance);
        assert(settings.enabled);
        assert(settings.firstClipmapExtent == 37.f);
        assert(settings.maximumLightDepth == 480.f);
        assert(settings.physicalPageCount == 2048u);
        assert(settings.preset == SvsmPreset::Performance);
        assert(settings.markingMode == SvsmMarkingMode::PerPixel);
        assert(settings.filterKernel ==
            SvsmFilterKernel::NearestPoisson);
        assert(settings.poissonOrdering ==
            SvsmPoissonOrdering::BalancedProgressive);
        assert(settings.tapCount == SvsmTapCount::Eight);
        assert(settings.resolutionBias == SvsmResolutionBias::PlusOne);
        assert(settings.adaptiveFiltering);
        assert(settings.staticPageRequestReuseEnabled);
        assert(settings.lightDepthOriginGuardBandEnabled);
        assert(settings.lightDepthOriginGuardBandFraction == 0.9f);
        assert(settings.allocationBudgetSaturationEarlyOutEnabled);
        assert(settings.finiteBudgetStaticDrainEnabled);
        assert(settings.staticVisibilityCachingEnabled);
        assert(settings.sceneStateCachingEnabled);
        assert(settings.casterOnlySceneRevisionEnabled);
        assert(settings.renderPacketCachingEnabled);
        assert(settings.sharedClipmapPacketBuilderEnabled);
        assert(settings.persistentCasterSourceCachingEnabled);
        assert(settings.opaqueRasterSpecializationEnabled);
        assert(settings.leanAlphaTestedBindingsEnabled);
        assert(settings.pairedStaticDynamicDepthEnabled);
        assert(settings.deferredStaticDepthMergeEnabled);
        assert(settings.movingLightUncachedEnabled);
        assert(settings.
            retainPhysicalMappingsOnContentInvalidationEnabled);
        assert(settings.movingLightLodBiasEnabled);
        assert(settings.movingLightResolutionBias ==
            SvsmResolutionBias::PlusTwo);
        assert(settings.movingLightLodRecoveryFrames ==
            SvsmDefaultMovingLightLodRecoveryFrames);
        assert(settings.receiverDistanceMipClampEnabled);
        assert(settings.receiverDistanceMipClampStartScale == 0.75f);
        assert(settings.receiverDistanceMipClampMaximumLevel ==
            SvsmMaximumReceiverDistanceMipClampLevel);
        assert(settings.movingLightContinuousReceiverBiasEnabled);
        assert(settings.localizedInvalidationEnabled);
        assert(settings.tightLocalizedInvalidationBoundsEnabled);
        assert(settings.adaptiveCasterCacheClassificationEnabled);
        assert(settings.gpuGatedDrawSubmission);
        assert(settings.batchedDrawSubmissionEnabled);
        assert(settings.packetStateSortingEnabled);
        assert(settings.levelEmptyWorkSkipEnabled);
        assert(settings.perPixelMarkingDedupeEnabled);
        assert(settings.packetPageCullingEnabled);
        assert(settings.hierarchicalScheduledPageMaskEnabled);
        assert(settings.receiverPageMaskCullingEnabled);
        assert(settings.staticDepthHierarchyCullingEnabled);
        assert(settings.staticDepthHierarchyBias == 0.0002f);
        assert(!settings.dirtyPageScatterRasterEnabled);
        assert(!settings.scatterAlphaTestEarlyRejectEnabled);
        assert(!settings.dirtyPageScatterAmplificationGuardEnabled);
        assert(settings.dirtyPageScatterMaximumAmplification == 4u);
        assert(settings.packetRectangleDirectScanEnabled);
        assert(settings.recentPageEvictionGraceEnabled);
        assert(settings.precomposedClipmapTransformsEnabled);
        assert(settings.pageTranslationCachingEnabled);
        assert(!settings.detailedGpuTimingEnabled);
        assert(settings.pageRenderBudget ==
            std::numeric_limits<uint32_t>::max());
        assert(!settings.coarsestPageRenderBudgetEnabled);
        assert(ValidateSvsmSettings(settings));

        ApplySvsmPreset(settings, SvsmPreset::Balanced);
        assert(settings.preset == SvsmPreset::Balanced);
        assert(settings.filterKernel == SvsmFilterKernel::BilinearPcf);
        assert(settings.poissonOrdering ==
            SvsmPoissonOrdering::BalancedProgressive);
        assert(settings.tapCount == SvsmTapCount::Four);
        assert(settings.resolutionBias == SvsmResolutionBias::Zero);
        assert(settings.adaptiveFiltering);
        assert(settings.receiverDistanceMipClampEnabled);
        assert(settings.receiverDistanceMipClampStartScale == 1.f);
        assert(settings.movingLightResolutionBias ==
            SvsmResolutionBias::PlusOne);
        assert(settings.lightDepthOriginGuardBandEnabled);
        assert(settings.lightDepthOriginGuardBandFraction == 0.9f);
        assert(settings.staticDepthHierarchyCullingEnabled);
        assert(ValidateSvsmSettings(settings));

        ApplySvsmPreset(settings, SvsmPreset::Quality);
        assert(settings.preset == SvsmPreset::Quality);
        assert(settings.filterKernel == SvsmFilterKernel::BilinearPcf);
        assert(settings.poissonOrdering ==
            SvsmPoissonOrdering::BalancedProgressive);
        assert(settings.tapCount == SvsmTapCount::Eight);
        assert(settings.resolutionBias == SvsmResolutionBias::Zero);
        assert(!settings.adaptiveFiltering);
        assert(!settings.receiverDistanceMipClampEnabled);
        assert(settings.receiverDistanceMipClampStartScale == 1.5f);
        assert(settings.movingLightResolutionBias ==
            SvsmResolutionBias::Zero);
        assert(settings.lightDepthOriginGuardBandEnabled);
        assert(settings.lightDepthOriginGuardBandFraction == 0.9f);
        assert(settings.staticDepthHierarchyCullingEnabled);
        assert(ValidateSvsmSettings(settings));

        settings.pageTranslationCachingEnabled = false;
        settings.preset = SvsmPreset::Custom;
        settings.finiteBudgetStaticDrainEnabled = true;
        settings.allocationBudgetSaturationEarlyOutEnabled = true;
        settings.detailedGpuTimingEnabled = false;
        assert(ValidateSvsmSettings(settings));
        settings.levelEmptyWorkSkipEnabled = true;
        assert(ValidateSvsmSettings(settings));
        settings.perPixelMarkingDedupeEnabled = true;
        assert(ValidateSvsmSettings(settings));
        settings.packetPageCullingEnabled = true;
        settings.dirtyPageScatterRasterEnabled = true;
        settings.scatterAlphaTestEarlyRejectEnabled = true;
        settings.dirtyPageScatterAmplificationGuardEnabled = true;
        settings.packetRectangleDirectScanEnabled = true;
        assert(ValidateSvsmSettings(settings));
        settings.dirtyPageScatterMaximumAmplification = 0u;
        assert(!ValidateSvsmSettings(settings));
        settings.dirtyPageScatterMaximumAmplification =
            SvsmMaximumDirtyPageScatterAmplification + 1u;
        assert(!ValidateSvsmSettings(settings));
        settings.dirtyPageScatterMaximumAmplification = 4u;
        settings.staticDepthHierarchyBias = -0.0001f;
        assert(!ValidateSvsmSettings(settings));
        settings.staticDepthHierarchyBias =
            std::numeric_limits<float>::quiet_NaN();
        assert(!ValidateSvsmSettings(settings));
        settings.staticDepthHierarchyBias = 0.0002f;
        settings.dirtyPageScatterMaximumAmplification = 4u;
        assert(ValidateSvsmSettings(settings));
        settings.lightDepthOriginGuardBandFraction = 0.f;
        assert(!ValidateSvsmSettings(settings));
        settings.lightDepthOriginGuardBandFraction =
            std::nextafter(
                1.f,
                std::numeric_limits<float>::infinity());
        assert(!ValidateSvsmSettings(settings));
        settings.lightDepthOriginGuardBandFraction =
            std::numeric_limits<float>::quiet_NaN();
        assert(!ValidateSvsmSettings(settings));
        settings.lightDepthOriginGuardBandFraction = 0.9f;
        assert(ValidateSvsmSettings(settings));
        settings.poissonOrdering =
            static_cast<SvsmPoissonOrdering>(
                std::numeric_limits<uint32_t>::max());
        assert(!ValidateSvsmSettings(settings));
        settings.poissonOrdering =
            SvsmPoissonOrdering::LegacyStride;
        assert(ValidateSvsmSettings(settings));
        settings.movingLightLodRecoveryFrames =
            SvsmMaximumMovingLightLodRecoveryFrames + 1u;
        assert(!ValidateSvsmSettings(settings));
        settings.movingLightLodRecoveryFrames =
            SvsmDefaultMovingLightLodRecoveryFrames;
        assert(ValidateSvsmSettings(settings));
        settings.receiverDistanceMipClampStartScale = 0.f;
        assert(!ValidateSvsmSettings(settings));
        settings.receiverDistanceMipClampStartScale =
            std::numeric_limits<float>::quiet_NaN();
        assert(!ValidateSvsmSettings(settings));
        settings.receiverDistanceMipClampStartScale = 1.5f;
        settings.receiverDistanceMipClampMaximumLevel =
            SvsmMaximumReceiverDistanceMipClampLevel + 1u;
        assert(!ValidateSvsmSettings(settings));
        settings.receiverDistanceMipClampMaximumLevel =
            SvsmMaximumReceiverDistanceMipClampLevel;
        assert(ValidateSvsmSettings(settings));
        settings.defaultObjectInvalidationMode =
            static_cast<SvsmObjectInvalidationMode>(
                std::numeric_limits<uint32_t>::max());
        assert(!ValidateSvsmSettings(settings));
        settings.defaultObjectInvalidationMode =
            SvsmObjectInvalidationMode::Auto;
        assert(ValidateSvsmSettings(settings));
        settings.pageRenderBudget = 0u;
        assert(ValidateSvsmSettings(settings));

    }

    void TestAlphaTestedRasterSpecialization()
    {
        assert(GetSvsmSparseAlphaBindingLayout(false) ==
            SvsmSparseAlphaBindingLayout::FullGBuffer);
        assert(GetSvsmSparseAlphaBindingLayout(true) ==
            SvsmSparseAlphaBindingLayout::ShadowOnly);
        assert(!RequiresSvsmSparseDepthPassRecreation(
            SvsmSparseAlphaBindingLayout::FullGBuffer,
            false,
            false,
            false));
        assert(RequiresSvsmSparseDepthPassRecreation(
            SvsmSparseAlphaBindingLayout::FullGBuffer,
            false,
            true,
            false));
        assert(RequiresSvsmSparseDepthPassRecreation(
            SvsmSparseAlphaBindingLayout::ShadowOnly,
            false,
            false,
            false));
        assert(!RequiresSvsmSparseDepthPassRecreation(
            SvsmSparseAlphaBindingLayout::ShadowOnly,
            false,
            true,
            false));
        assert(RequiresSvsmSparseDepthPassRecreation(
            SvsmSparseAlphaBindingLayout::ShadowOnly,
            false,
            true,
            true));
        assert(!RequiresSvsmSparseDepthPassRecreation(
            SvsmSparseAlphaBindingLayout::ShadowOnly,
            true,
            true,
            true));
        assert(GetSvsmDeferredStaticDepthPassAttempt(
                true, true, true) ==
            SvsmDeferredStaticDepthPassAttempt::
                DeferredThenReference);
        assert(GetSvsmDeferredStaticDepthPassAttempt(
                true, true, false) ==
            SvsmDeferredStaticDepthPassAttempt::ReferenceOnly);
        assert(GetSvsmDeferredStaticDepthPassAttempt(
                true, false, true) ==
            SvsmDeferredStaticDepthPassAttempt::ReferenceOnly);
        assert(GetSvsmDeferredStaticDepthPassAttempt(
                false, true, true) ==
            SvsmDeferredStaticDepthPassAttempt::ReferenceOnly);
        assert(ShouldFallbackSvsmDeferredStaticDepthPass(
            SvsmDeferredStaticDepthPassAttempt::
                DeferredThenReference,
            false));
        assert(!ShouldFallbackSvsmDeferredStaticDepthPass(
            SvsmDeferredStaticDepthPassAttempt::
                DeferredThenReference,
            true));
        assert(!ShouldFallbackSvsmDeferredStaticDepthPass(
            SvsmDeferredStaticDepthPassAttempt::ReferenceOnly,
            false));
        assert(IsSvsmDeferredStaticDepthMergeRequestEffective(
            true, false));
        assert(!IsSvsmDeferredStaticDepthMergeRequestEffective(
            true, true));
        assert(!IsSvsmDeferredStaticDepthMergeRequestEffective(
            false, false));
        assert(ShouldLatchSvsmDeferredStaticDepthRasterFallback(
            true, false));
        assert(!ShouldLatchSvsmDeferredStaticDepthRasterFallback(
            true, true));
        assert(!ShouldLatchSvsmDeferredStaticDepthRasterFallback(
            false, false));
        assert(GetNextSvsmDeferredStaticDepthRasterFallbackLatched(
            false, true, true, false));
        assert(GetNextSvsmDeferredStaticDepthRasterFallbackLatched(
            true, true, false, true));
        assert(!GetNextSvsmDeferredStaticDepthRasterFallbackLatched(
            true, false, false, true));

        assert(EvaluateSvsmAlphaTestOpacity(
            0.75f, false, 0.1f, false, 0.2f) == 0.75f);
        assert(EvaluateSvsmAlphaTestOpacity(
            0.5f, false, 0.9f, true, 0.25f) == 0.125f);
        // Explicit opacity is authoritative and base alpha must not be sampled.
        assert(EvaluateSvsmAlphaTestOpacity(
            0.5f, true, 0.8f, true, 0.01f) == 0.4f);
        assert(EvaluateSvsmAlphaTestOpacity(
            2.f, true, 0.75f, false, 0.f) == 1.f);
        assert(EvaluateSvsmAlphaTestOpacity(
            -1.f, false, 1.f, false, 1.f) == 0.f);

        assert(HasSvsmAlphaTestScalarDepthChange(
            true, 1.f, 0.5f, 0.5f, 0.5f));
        assert(HasSvsmAlphaTestScalarDepthChange(
            true, 1.f, 1.f, 0.5f, 0.25f));
        assert(!HasSvsmAlphaTestScalarDepthChange(
            false, 1.f, 0.5f, 0.5f, 0.25f));

        SvsmCasterEvent alphaOpacityEvent;
        alphaOpacityEvent.depthMaterialChanged =
            HasSvsmAlphaTestScalarDepthChange(
                true, 1.f, 0.5f, 0.5f, 0.5f);
        const SvsmCasterEventDecision alphaDecision =
            ReconcileSvsmCasterEvent(
                SvsmObjectInvalidationMode::Auto,
                alphaOpacityEvent);
        assert(alphaDecision.category ==
            SvsmCasterEventCategory::Invalidating);

        SvsmCasterEvent opaqueOpacityEvent;
        opaqueOpacityEvent.depthMaterialChanged =
            HasSvsmAlphaTestScalarDepthChange(
                false, 1.f, 0.5f, 0.5f, 0.5f);
        const SvsmCasterEventDecision opaqueDecision =
            ReconcileSvsmCasterEvent(
                SvsmObjectInvalidationMode::Auto,
                opaqueOpacityEvent);
        assert(opaqueDecision.category ==
            SvsmCasterEventCategory::Unchanged);
    }

    void TestTapCountPermutationSelection()
    {
        assert(SvsmTapCountPermutationIndex(
            SvsmTapCount::One) == 0u);
        assert(SvsmTapCountPermutationIndex(
            SvsmTapCount::Four) == 1u);
        assert(SvsmTapCountPermutationIndex(
            SvsmTapCount::Eight) == 2u);
        assert(SvsmTapCountPermutationIndex(
            SvsmTapCount::Sixteen) == 3u);
    }

    void TestCachedShadowDrawListPolicy()
    {
        assert(!ShouldUseSvsmShadowDrawLists(false, false));
        assert(ShouldUseSvsmShadowDrawLists(true, false));
        assert(ShouldUseSvsmShadowDrawLists(false, true));
        assert(ShouldUseSvsmShadowDrawLists(true, true));

        // Content invalidation is not packet invalidation. Every combination
        // of the former outer-gate inputs is allowed to reach the exact packet
        // key whenever cached draw lists are enabled.
        for (uint32_t mask = 0u; mask < 128u; ++mask)
        {
            const bool cached = (mask & 1u) == 0u;
            assert(CanAttemptSvsmRenderPacketReuse(
                cached) == cached);
        }
        // A content-only invalidation with an unchanged packet key reuses the
        // list. Any real key difference still rebuilds.
        assert(ShouldReuseSvsmRenderPackets(true, true));
        assert(!ShouldReuseSvsmRenderPackets(true, false));
        assert(!ShouldReuseSvsmRenderPackets(false, true));
        assert(!ShouldReuseSvsmRenderPackets(false, false));

        const SvsmRasterSubmissionTransactionAction sparseSuccess =
            GetSvsmRasterSubmissionTransactionAction(true, true);
        assert(sparseSuccess.publishDepth);
        assert(sparseSuccess.publishStaticDepthHierarchy);
        assert(sparseSuccess.finalizePages);
        assert(sparseSuccess.resolveVisibility);
        assert(sparseSuccess.commitCacheState);
        assert(!sparseSuccess.returnWhite);
        assert(!sparseSuccess.latchFullRebuild);
        assert(!sparseSuccess.clearSparseResources);
        assert(!sparseSuccess.invalidateVisibilityCaches);
        assert(!sparseSuccess.resetDepthBindings);

        const SvsmRasterSubmissionTransactionAction sparseFailure =
            GetSvsmRasterSubmissionTransactionAction(false, true);
        assert(!sparseFailure.publishDepth);
        assert(!sparseFailure.publishStaticDepthHierarchy);
        assert(!sparseFailure.finalizePages);
        assert(!sparseFailure.resolveVisibility);
        assert(!sparseFailure.commitCacheState);
        assert(sparseFailure.returnWhite);
        assert(sparseFailure.latchFullRebuild);
        assert(sparseFailure.clearSparseResources);
        assert(sparseFailure.invalidateVisibilityCaches);
        assert(sparseFailure.resetDepthBindings);

        const SvsmRasterSubmissionTransactionAction denseFailure =
            GetSvsmRasterSubmissionTransactionAction(false, false);
        assert(denseFailure.returnWhite);
        assert(!denseFailure.latchFullRebuild);
        assert(!denseFailure.clearSparseResources);
        assert(denseFailure.invalidateVisibilityCaches);
        assert(denseFailure.resetDepthBindings);

        for (uint32_t mask = 0u; mask < 8u; ++mask)
        {
            const bool discarded = (mask & 1u) != 0u;
            const bool tagged = (mask & 2u) != 0u;
            const bool queueHasCapacity = (mask & 4u) != 0u;
            const SvsmTimerRetirementAction action =
                GetSvsmTimerRetirementAction(
                    discarded, tagged, queueHasCapacity);
            assert(action.allowUiPublication == !discarded);
            assert(action.retireTaggedSample == tagged);
            assert(action.enqueueTaggedSample ==
                (tagged && !discarded && queueHasCapacity));
            assert(action.dropTaggedSample ==
                (tagged && (discarded || !queueHasCapacity)));
            assert(!(action.enqueueTaggedSample &&
                action.dropTaggedSample));
        }

        assert(ClassifySvsmPacketDrawItem(
            false, false, false, false, false,
            false, false, false, false, false) ==
            SvsmPacketDrawItemDisposition::Skip);
        for (uint32_t mask = 0u; mask < 511u; ++mask)
        {
            assert(ClassifySvsmPacketDrawItem(
                true,
                (mask & (1u << 0u)) != 0u,
                (mask & (1u << 1u)) != 0u,
                (mask & (1u << 2u)) != 0u,
                (mask & (1u << 3u)) != 0u,
                (mask & (1u << 4u)) != 0u,
                (mask & (1u << 5u)) != 0u,
                (mask & (1u << 6u)) != 0u,
                (mask & (1u << 7u)) != 0u,
                (mask & (1u << 8u)) != 0u) ==
                SvsmPacketDrawItemDisposition::Abort);
        }
        assert(ClassifySvsmPacketDrawItem(
            true, true, true, true, true,
            true, true, true, true, true) ==
            SvsmPacketDrawItemDisposition::Accept);

        assert(GetNextSvsmPacketClassificationGeneration(0u) == 1u);
        assert(GetNextSvsmPacketClassificationGeneration(41u) == 42u);
        assert(GetNextSvsmPacketClassificationGeneration(
            std::numeric_limits<uint64_t>::max()) == 1u);

        assert(KeepSvsmIndirectArgumentTemplatesInitialized(
            true, false));
        assert(!KeepSvsmIndirectArgumentTemplatesInitialized(
            true, true));
        assert(!KeepSvsmIndirectArgumentTemplatesInitialized(
            false, false));
        assert(!KeepSvsmIndirectArgumentTemplatesInitialized(
            false, true));
    }

    void TestBatchedDrawPackingAndGrouping()
    {
        assert(SvsmDebugCounterCount == 17u);
        assert(SvsmLevelRenderCounterBase == 17u);
        assert(SvsmAllocatorControlCounterBase == 23u);
        assert(SvsmLevelHasWorkCounterBase == 31u);
        assert(SvsmLevelHasWorkCounterCount == SvsmClipmapCount);
        assert(SvsmScheduledTileMaskCounterBase == 37u);
        assert(
            SvsmClipmapCount *
                SvsmScheduledTileMaskWordsPerLevel *
                sizeof(uint32_t) ==
            120u);
        assert(SvsmScheduledTileMaskQueryCounter == 37u);
        assert(SvsmScheduledTileMaskEarlyRejectCounter == 38u);
        assert(SvsmScheduledTileMaskFailOpenCounter == 39u);
        assert(
            SvsmScheduledTileMaskPositiveExactZeroCounter == 40u);
        assert(SvsmStaticDepthHierarchyQueryCounter == 41u);
        assert(SvsmStaticDepthHierarchyCullCounter == 42u);
        assert(SvsmStaticDepthHierarchyFailOpenCounter == 43u);
        assert(SvsmStaticDepthHierarchyBuiltPageCounter == 44u);
        assert(SvsmReceiverPageMaskQueryCounter == 45u);
        assert(SvsmReceiverPageMaskCullCounter == 46u);
        assert(SvsmReceiverPageMaskFailOpenCounter == 47u);
        assert(SvsmCounterCount == 48u);
        assert(GetSvsmLevelHasWorkCounterIndex(0u) == 31u);
        assert(GetSvsmLevelHasWorkCounterIndex(
            SvsmClipmapCount - 1u) == 36u);
        assert(GetSvsmLevelHasWorkCounterIndex(
            SvsmClipmapCount - 1u) + 1u ==
            SvsmScheduledTileMaskCounterBase);
        assert(EncodeSvsmLevelHasWorkIndirectCount(0u, 0u) == 0u);
        assert(EncodeSvsmLevelHasWorkIndirectCount(0u, 23u) == 0u);
        assert(EncodeSvsmLevelHasWorkIndirectCount(1u, 0u) == 0u);
        assert(EncodeSvsmLevelHasWorkIndirectCount(1u, 23u) == 23u);
        assert(EncodeSvsmLevelHasWorkIndirectCount(4096u, 23u) == 23u);
        assert(EncodeSvsmLevelHasWorkIndirectCount(
            1u, std::numeric_limits<uint32_t>::max()) ==
            std::numeric_limits<uint32_t>::max());
        assert(EncodeSvsmLevelHasWorkIndirectCount(
            0u, std::numeric_limits<uint32_t>::max()) == 0u);
        constexpr uint32_t packetGroupCount = 7u;
        assert(std::min(
            EncodeSvsmLevelHasWorkIndirectCount(0u, 23u),
            packetGroupCount) == 0u);
        assert(std::min(
            EncodeSvsmLevelHasWorkIndirectCount(1u, 23u),
            packetGroupCount) == packetGroupCount);
        constexpr uint32_t secondPacketGroupCount = 16u;
        assert(std::min(
            EncodeSvsmLevelHasWorkIndirectCount(1u, 23u),
            secondPacketGroupCount) == secondPacketGroupCount);
        assert(packetGroupCount + secondPacketGroupCount == 23u);
        assert(std::min(
            EncodeSvsmLevelHasWorkIndirectCount(1u, 23u),
            23u) == 23u);
        constexpr uint32_t allocationDispatchGate =
            SvsmLevelHasWorkDispatchGate;
        static_assert(allocationDispatchGate == 1u);
        constexpr uint32_t promotedDrawCount =
            EncodeSvsmLevelHasWorkIndirectCount(1u, 23u);
        static_assert(promotedDrawCount == 23u);
        constexpr uint32_t exactDirtyPageCount = 1u;
        assert(std::min(
            exactDirtyPageCount,
            packetGroupCount) == exactDirtyPageCount);
        assert(exactDirtyPageCount != packetGroupCount);

        constexpr uint32_t poolPages = 4096u;
        constexpr uint32_t maximumObject =
            (std::numeric_limits<uint32_t>::max() -
                (poolPages - 1u)) /
            poolPages;
        assert(CanEncodeSvsmBatchedDraw(
            uint32_t(std::numeric_limits<int32_t>::max()),
            maximumObject,
            poolPages));
        assert(!CanEncodeSvsmBatchedDraw(
            uint32_t(std::numeric_limits<int32_t>::max()) + 1u,
            0u,
            poolPages));
        assert(!CanEncodeSvsmBatchedDraw(0u, 0u, 0u));
        if (maximumObject < std::numeric_limits<uint32_t>::max())
        {
            assert(!CanEncodeSvsmBatchedDraw(
                0u, maximumObject + 1u, poolPages));
        }

        const uint32_t encodedStart =
            EncodeSvsmBatchedStartInstance(
                maximumObject, poolPages);
        assert(DecodeSvsmBatchedObjectIndex(
            encodedStart, poolPages) == maximumObject);
        assert(IsSvsmBatchedStartInstanceEncodingValid(
            encodedStart, poolPages));
        assert(!IsSvsmBatchedStartInstanceEncodingValid(
            encodedStart + 1u, poolPages));
        assert(EncodeSvsmBatchedBaseVertex(1234u) == 1234);

        constexpr SvsmBatchedDrawStateKey opaqueState =
            MakeSvsmBatchedDrawStateKey(11u, 22u, 1u, false);
        assert(opaqueState ==
            MakeSvsmBatchedDrawStateKey(11u, 23u, 1u, false));
        assert(!(opaqueState ==
            MakeSvsmBatchedDrawStateKey(12u, 22u, 1u, false)));
        assert(!(opaqueState ==
            MakeSvsmBatchedDrawStateKey(11u, 22u, 2u, false)));
        assert(!(opaqueState ==
            MakeSvsmBatchedDrawStateKey(11u, 22u, 1u, true)));
        assert(!(MakeSvsmBatchedDrawStateKey(11u, 22u, 1u, true) ==
            MakeSvsmBatchedDrawStateKey(11u, 23u, 1u, true)));

        constexpr SvsmPacketStateSortKey opaqueSortA =
            MakeSvsmPacketStateSortKey(opaqueState, 22u, true);
        constexpr SvsmPacketStateSortKey opaqueSortB =
            MakeSvsmPacketStateSortKey(opaqueState, 23u, true);
        assert(opaqueSortA == opaqueSortB);
        assert(CanMergeSvsmPacketStateGroup(
            opaqueState, true, opaqueState, true));
        assert(!CanMergeSvsmPacketStateGroup(
            opaqueState, false, opaqueState, true));
        assert(!CanMergeSvsmPacketStateGroup(
            opaqueState, false, opaqueState, false));
        assert(!CanMergeSvsmPacketStateGroup(
            opaqueState, true,
            MakeSvsmBatchedDrawStateKey(12u, 22u, 1u, false), true));
        assert(!CanMergeSvsmPacketStateGroup(
            opaqueState, true,
            MakeSvsmBatchedDrawStateKey(11u, 22u, 2u, false), true));
        assert(!CanMergeSvsmPacketStateGroup(
            opaqueState, true,
            MakeSvsmBatchedDrawStateKey(11u, 22u, 1u, true), true));
        assert(!IsSvsmPacketStateSortKeyLess(
            opaqueSortA, opaqueSortB));
        assert(!IsSvsmPacketStateSortKeyLess(
            opaqueSortB, opaqueSortA));

        constexpr SvsmBatchedDrawStateKey alphaStateA =
            MakeSvsmBatchedDrawStateKey(11u, 22u, 1u, true);
        constexpr SvsmBatchedDrawStateKey alphaStateB =
            MakeSvsmBatchedDrawStateKey(11u, 23u, 1u, true);
        assert(!(MakeSvsmPacketStateSortKey(
            alphaStateA, 22u, true) ==
            MakeSvsmPacketStateSortKey(alphaStateB, 23u, true)));
        assert(!(MakeSvsmPacketStateSortKey(
            opaqueState, 22u, false) ==
            MakeSvsmPacketStateSortKey(opaqueState, 23u, false)));
        assert(!(MakeSvsmPacketStateSortKey(
            opaqueState, 22u, true) ==
            MakeSvsmPacketStateSortKey(opaqueState, 22u, false)));

        struct PacketModel
        {
            SvsmPacketStateSortKey key;
            uint32_t originalOrder;
            uint32_t pageListOffset;
            uint32_t objectInstanceIndex;
            uint32_t argumentIndex = 0u;
        };
        const SvsmBatchedDrawStateKey bufferA =
            MakeSvsmBatchedDrawStateKey(10u, 0u, 1u, false);
        const SvsmBatchedDrawStateKey bufferB =
            MakeSvsmBatchedDrawStateKey(20u, 0u, 1u, false);
        std::vector<PacketModel> packets = {
            { MakeSvsmPacketStateSortKey(bufferB, 50u, true),
                0u, 100u, 200u },
            { MakeSvsmPacketStateSortKey(bufferA, 60u, true),
                1u, 101u, 201u },
            { MakeSvsmPacketStateSortKey(bufferA, 70u, true),
                2u, 102u, 202u },
            { MakeSvsmPacketStateSortKey(bufferA, 80u, false),
                3u, 103u, 203u },
            { MakeSvsmPacketStateSortKey(bufferA, 90u, false),
                4u, 104u, 204u }
        };
        std::stable_sort(
            packets.begin(), packets.end(),
            [](const PacketModel& left, const PacketModel& right) {
                return IsSvsmPacketStateSortKeyLess(
                    left.key, right.key);
            });
        assert(packets[0].originalOrder == 1u);
        assert(packets[1].originalOrder == 2u);
        assert(packets[0].pageListOffset == 101u);
        assert(packets[0].objectInstanceIndex == 201u);
        assert(packets[1].pageListOffset == 102u);
        assert(packets[1].objectInstanceIndex == 202u);
        assert(packets[2].originalOrder == 3u);
        assert(packets[3].originalOrder == 4u);
        assert(packets[4].originalOrder == 0u);
        constexpr uint32_t clipmapPacketOffset = 37u;
        for (uint32_t packetIndex = 0u;
            packetIndex < uint32_t(packets.size());
            ++packetIndex)
        {
            packets[packetIndex].argumentIndex =
                clipmapPacketOffset + packetIndex;
            assert(packets[packetIndex].argumentIndex ==
                clipmapPacketOffset + packetIndex);
        }
        constexpr std::array<uint32_t, 4> clipmapPacketCounts = {
            3u, 0u, 5u, 2u
        };
        std::array<uint32_t, 4> clipmapPacketOffsets{};
        uint32_t globalPacketCount = 0u;
        for (uint32_t level = 0u;
            level < uint32_t(clipmapPacketCounts.size());
            ++level)
        {
            clipmapPacketOffsets[level] = globalPacketCount;
            globalPacketCount += clipmapPacketCounts[level];
            assert(clipmapPacketOffsets[level] +
                clipmapPacketCounts[level] == globalPacketCount);
        }
        assert(clipmapPacketOffsets[0] == 0u);
        assert(clipmapPacketOffsets[1] == 3u);
        assert(clipmapPacketOffsets[2] == 3u);
        assert(clipmapPacketOffsets[3] == 8u);
        assert(globalPacketCount == 10u);

        // Only the two batchable opaque packets share a group. The exact
        // nonbatchable material identity keeps both fallback packets in
        // singleton groups even though their other state is compatible.
        uint32_t groupCount = 0u;
        bool previousBatchable = false;
        SvsmBatchedDrawStateKey previousState{};
        for (uint32_t packetIndex = 0u;
            packetIndex < uint32_t(packets.size());
            ++packetIndex)
        {
            const bool batchable = packets[packetIndex].key.batchable;
            const SvsmBatchedDrawStateKey state = {
                packets[packetIndex].key.bufferGroup,
                packets[packetIndex].key.alphaTested
                    ? packets[packetIndex].key.material
                    : 0u,
                packets[packetIndex].key.cullMode,
                packets[packetIndex].key.alphaTested
            };
            if (packetIndex == 0u ||
                !CanMergeSvsmPacketStateGroup(
                    previousState, previousBatchable,
                    state, batchable))
            {
                ++groupCount;
            }
            previousState = state;
            previousBatchable = batchable;
        }
        assert(groupCount == 4u);

        // The GPU fill stage writes the same rendered-page count to every
        // indirect packet in a group. With no dirty pages every multi-draw
        // command therefore contains only zero-instance draws.
        std::array<uint32_t, 3> instanceCounts{};
        const uint32_t dirtyPageCount = 0u;
        instanceCounts.fill(dirtyPageCount);
        for (const uint32_t instanceCount : instanceCounts)
            assert(instanceCount == 0u);
    }

    void TestMotionBenchmarkSequence()
    {
        SvsmMotionBenchmarkPathObservation disabledPath;
        ObserveSvsmMotionBenchmarkPath(
            disabledPath, false, false, false);
        assert(!disabledPath.requestedObserved);
        assert(!disabledPath.activeObserved);
        assert(!disabledPath.inactiveObserved);
        assert(!disabledPath.unavailableObserved);

        SvsmMotionBenchmarkPathObservation mixedPath;
        ObserveSvsmMotionBenchmarkPath(
            mixedPath, true, true, false);
        ObserveSvsmMotionBenchmarkPath(
            mixedPath, true, false, false);
        ObserveSvsmMotionBenchmarkPath(
            mixedPath, true, false, true);
        assert(mixedPath.requestedObserved);
        assert(mixedPath.activeObserved);
        assert(mixedPath.inactiveObserved);
        assert(mixedPath.unavailableObserved);

        SvsmMotionBenchmarkPathObservation unavailablePath;
        ObserveSvsmMotionBenchmarkPath(
            unavailablePath, true, false, true);
        assert(unavailablePath.requestedObserved);
        assert(!unavailablePath.activeObserved);
        assert(!unavailablePath.inactiveObserved);
        assert(unavailablePath.unavailableObserved);

        const SvsmMotionBenchmarkTimingSummary emptySummary =
            SummarizeSvsmMotionBenchmarkSamples({});
        assert(emptySummary.sampleCount == 0u);
        assert(emptySummary.median == 0.f);
        assert(emptySummary.p95 == 0.f);
        assert(emptySummary.p99 == 0.f);
        assert(emptySummary.maximum == 0.f);

        const SvsmMotionBenchmarkTimingSummary oddSummary =
            SummarizeSvsmMotionBenchmarkSamples({ 9.f, 1.f, 5.f });
        assert(oddSummary.sampleCount == 3u);
        assert(oddSummary.median == 5.f);
        assert(oddSummary.p95 == 9.f);
        assert(oddSummary.p99 == 9.f);
        assert(oddSummary.maximum == 9.f);

        const SvsmMotionBenchmarkTimingSummary percentileSummary =
            SummarizeSvsmMotionBenchmarkSamples({
                20.f, 19.f, 18.f, 17.f, 16.f,
                15.f, 14.f, 13.f, 12.f, 11.f,
                10.f, 9.f, 8.f, 7.f, 6.f,
                5.f, 4.f, 3.f, 2.f, 1.f
            });
        assert(percentileSummary.sampleCount == 20u);
        assert(percentileSummary.median == 10.5f);
        assert(percentileSummary.p95 == 19.f);
        assert(percentileSummary.p99 == 20.f);
        assert(percentileSummary.maximum == 20.f);

        const std::vector<float> targetSamples = {
            0.4f, 0.4001f, 0.7f, 0.7001f, 1.0001f
        };
        assert(CountSvsmMotionBenchmarkSamplesAbove(
            targetSamples, 0.4f) == 4u);
        assert(CountSvsmMotionBenchmarkSamplesAbove(
            targetSamples, 0.7f) == 2u);
        assert(CountSvsmMotionBenchmarkSamplesAbove(
            targetSamples, 1.f) == 1u);
        const SvsmMotionBenchmarkTimingSummary passingTarget = {
            3u, 0.4f, 0.5f, 0.6f, 0.7f
        };
        assert(IsSvsmMotionBenchmarkGpuTargetMet(
            true, true, passingTarget));
        assert(!IsSvsmMotionBenchmarkGpuTargetMet(
            false, true, passingTarget));
        assert(!IsSvsmMotionBenchmarkGpuTargetMet(
            true, false, passingTarget));
        SvsmMotionBenchmarkTimingSummary failingMedian = passingTarget;
        failingMedian.median = 0.4001f;
        assert(!IsSvsmMotionBenchmarkGpuTargetMet(
            true, true, failingMedian));
        SvsmMotionBenchmarkTimingSummary failingSpike = passingTarget;
        failingSpike.maximum = 0.7001f;
        assert(!IsSvsmMotionBenchmarkGpuTargetMet(
            true, true, failingSpike));

        const float stageSum = SumSvsmMotionBenchmarkGpuStages(
            0.01f, 0.02f, 0.03f, 0.04f, 0.05f, 0.06f);
        assert(std::abs(stageSum - 0.21f) < 1e-7f);

        assert(SvsmMotionBenchmarkTurnFrames == 450u);
        assert(SvsmMotionBenchmarkHoldFrames == 100u);
        assert(SvsmMotionBenchmarkEndFrame == 1180u);
        assert(std::abs(
            SvsmMotionBenchmarkDegreesPerFrame - 0.1f) < 1e-7f);
        assert(GetSvsmMotionBenchmarkSegment(0u) ==
            SvsmMotionBenchmarkSegment::Warm);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(
                SvsmMotionBenchmarkWarmFrames - 1u)) < 1e-7f);
        for (uint64_t frame = 0u;
            frame < SvsmMotionBenchmarkWarmFrames;
            ++frame)
        {
            assert(std::abs(
                GetSvsmMotionBenchmarkAngleDegrees(frame)) < 1e-7f);
        }

        const uint64_t firstTurnFrame =
            SvsmMotionBenchmarkWarmFrames;
        const uint64_t firstHoldFrame =
            firstTurnFrame + SvsmMotionBenchmarkTurnFrames;
        const uint64_t firstReturnFrame =
            firstHoldFrame + SvsmMotionBenchmarkHoldFrames;
        assert(
            SvsmMotionBenchmarkEndFrame -
                SvsmMotionBenchmarkWarmFrames ==
            1000u);
        assert(SvsmMotionBenchmarkMeasurementFrames == 1000u);
        assert(!IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkWarmFrames - 1u));
        assert(IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkWarmFrames));
        assert(IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkEndFrame - 1u));
        assert(!IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkEndFrame));
        assert(GetSvsmMotionBenchmarkSegment(firstTurnFrame) ==
            SvsmMotionBenchmarkSegment::TurnRight);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(firstTurnFrame) -
                0.1f) < 1e-6f);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(
                firstTurnFrame +
                    SvsmMotionBenchmarkTurnFrames - 1u) -
                45.f) < 1e-5f);
        for (uint64_t frame = firstTurnFrame + 1u;
            frame < firstHoldFrame;
            ++frame)
        {
            const float delta =
                GetSvsmMotionBenchmarkAngleDegrees(frame) -
                GetSvsmMotionBenchmarkAngleDegrees(frame - 1u);
            assert(std::abs(delta - 0.1f) < 1e-5f);
        }

        assert(GetSvsmMotionBenchmarkSegment(firstHoldFrame) ==
            SvsmMotionBenchmarkSegment::HoldRight);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(firstHoldFrame) -
                45.f) < 1e-7f);
        for (uint64_t frame = firstHoldFrame;
            frame < firstReturnFrame;
            ++frame)
        {
            assert(std::abs(
                GetSvsmMotionBenchmarkAngleDegrees(frame) -
                    45.f) < 1e-7f);
        }

        assert(GetSvsmMotionBenchmarkSegment(firstReturnFrame) ==
            SvsmMotionBenchmarkSegment::TurnBack);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(firstReturnFrame) -
                44.9f) < 1e-5f);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(
                SvsmMotionBenchmarkEndFrame - 1u)) < 1e-5f);
        for (uint64_t frame = firstReturnFrame + 1u;
            frame < SvsmMotionBenchmarkEndFrame;
            ++frame)
        {
            const float delta =
                GetSvsmMotionBenchmarkAngleDegrees(frame) -
                GetSvsmMotionBenchmarkAngleDegrees(frame - 1u);
            assert(std::abs(delta + 0.1f) < 1e-5f);
        }
        assert(GetSvsmMotionBenchmarkSegment(
            SvsmMotionBenchmarkEndFrame) ==
            SvsmMotionBenchmarkSegment::Complete);
        assert(std::abs(
            GetSvsmMotionBenchmarkAngleDegrees(
                SvsmMotionBenchmarkEndFrame)) < 1e-7f);

        assert(SvsmSunMotionBenchmarkEndFrame == 1408u);
        assert(SvsmSunMotionBenchmarkMeasurementFrames == 1288u);
        assert(SvsmMotionBenchmarkMaximumMeasurementFrames == 1288u);
        assert(GetSvsmMotionBenchmarkWarmFrameCount(
            SvsmMotionBenchmarkKind::Camera) ==
            SvsmMotionBenchmarkWarmFrames);
        assert(GetSvsmMotionBenchmarkWarmFrameCount(
            SvsmMotionBenchmarkKind::SunSlow) ==
            SvsmSunMotionBenchmarkWarmFrames);
        assert(GetSvsmMotionBenchmarkEndFrame(
            SvsmMotionBenchmarkKind::Camera) ==
            SvsmMotionBenchmarkEndFrame);
        assert(GetSvsmMotionBenchmarkEndFrame(
            SvsmMotionBenchmarkKind::SunSlow) ==
            SvsmSunMotionBenchmarkEndFrame);

        const uint64_t sunBaselineBegin =
            SvsmSunMotionBenchmarkWarmFrames;
        const uint64_t sunForwardBegin =
            sunBaselineBegin +
            SvsmSunMotionBenchmarkBaselineFrames;
        const uint64_t sunRecoveryBegin =
            sunForwardBegin +
            SvsmSunMotionBenchmarkTurnFrames;
        const uint64_t sunReverseBegin =
            sunRecoveryBegin +
            SvsmSunMotionBenchmarkRecoveryFrames;
        const uint64_t sunFinalRecoveryBegin =
            sunReverseBegin +
            SvsmSunMotionBenchmarkTurnFrames;
        assert(GetSvsmMotionBenchmarkPhase(
            SvsmMotionBenchmarkKind::SunSlow,
            sunBaselineBegin) ==
            SvsmMotionBenchmarkPhase::Baseline);
        assert(GetSvsmMotionBenchmarkPhase(
            SvsmMotionBenchmarkKind::SunSlow,
            sunForwardBegin) ==
            SvsmMotionBenchmarkPhase::Forward);
        assert(GetSvsmMotionBenchmarkPhase(
            SvsmMotionBenchmarkKind::SunSlow,
            sunRecoveryBegin) ==
            SvsmMotionBenchmarkPhase::Recovery);
        assert(GetSvsmMotionBenchmarkPhase(
            SvsmMotionBenchmarkKind::SunSlow,
            sunReverseBegin) ==
            SvsmMotionBenchmarkPhase::Reverse);
        assert(GetSvsmMotionBenchmarkPhase(
            SvsmMotionBenchmarkKind::SunSlow,
            sunFinalRecoveryBegin) ==
            SvsmMotionBenchmarkPhase::FinalRecovery);
        assert(GetSvsmMotionBenchmarkPhase(
            SvsmMotionBenchmarkKind::SunSlow,
            SvsmSunMotionBenchmarkEndFrame) ==
            SvsmMotionBenchmarkPhase::Complete);
        assert(GetSvsmMotionBenchmarkTenthDegreeTicks(
            SvsmMotionBenchmarkKind::SunSlow,
            sunForwardBegin) == 1);
        assert(GetSvsmMotionBenchmarkTenthDegreeTicks(
            SvsmMotionBenchmarkKind::SunSlow,
            sunRecoveryBegin - 1u) == 450);
        assert(GetSvsmMotionBenchmarkTenthDegreeTicks(
            SvsmMotionBenchmarkKind::SunSlow,
            sunRecoveryBegin) == 450);
        assert(GetSvsmMotionBenchmarkTenthDegreeTicks(
            SvsmMotionBenchmarkKind::SunSlow,
            sunReverseBegin) == 449);
        assert(GetSvsmMotionBenchmarkTenthDegreeTicks(
            SvsmMotionBenchmarkKind::SunSlow,
            sunFinalRecoveryBegin - 1u) == 0);
        assert(GetSvsmMotionBenchmarkTenthDegreeTicks(
            SvsmMotionBenchmarkKind::SunSlow,
            sunFinalRecoveryBegin) == 0);
        for (uint64_t frame = sunForwardBegin + 1u;
            frame < sunRecoveryBegin;
            ++frame)
        {
            assert(GetSvsmMotionBenchmarkTenthDegreeTicks(
                SvsmMotionBenchmarkKind::SunSlow,
                frame) -
                GetSvsmMotionBenchmarkTenthDegreeTicks(
                    SvsmMotionBenchmarkKind::SunSlow,
                    frame - 1u) == 1);
            assert(IsSvsmMotionBenchmarkDirectionUpdateFrame(
                SvsmMotionBenchmarkKind::SunSlow,
                frame));
        }
        for (uint64_t frame = sunRecoveryBegin;
            frame < sunReverseBegin;
            ++frame)
        {
            assert(GetSvsmMotionBenchmarkTenthDegreeTicks(
                SvsmMotionBenchmarkKind::SunSlow,
                frame) == 450);
            assert(!IsSvsmMotionBenchmarkDirectionUpdateFrame(
                SvsmMotionBenchmarkKind::SunSlow,
                frame));
        }
        for (uint64_t frame = sunReverseBegin + 1u;
            frame < sunFinalRecoveryBegin;
            ++frame)
        {
            assert(GetSvsmMotionBenchmarkTenthDegreeTicks(
                SvsmMotionBenchmarkKind::SunSlow,
                frame) -
                GetSvsmMotionBenchmarkTenthDegreeTicks(
                    SvsmMotionBenchmarkKind::SunSlow,
                    frame - 1u) == -1);
            assert(IsSvsmMotionBenchmarkDirectionUpdateFrame(
                SvsmMotionBenchmarkKind::SunSlow,
                frame));
        }
        assert(!IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkKind::SunSlow,
            SvsmSunMotionBenchmarkWarmFrames - 1u));
        assert(IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkKind::SunSlow,
            SvsmSunMotionBenchmarkWarmFrames));
        assert(IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkKind::SunSlow,
            SvsmSunMotionBenchmarkEndFrame - 1u));
        assert(!IsSvsmMotionBenchmarkMeasurementFrame(
            SvsmMotionBenchmarkKind::SunSlow,
            SvsmSunMotionBenchmarkEndFrame));
        assert(IsSvsmMotionBenchmarkEvidenceValidForFrameCount(
            SvsmSunMotionBenchmarkMeasurementFrames,
            1288u, 1288u, 1288u, 0u, 1288u, 0u, false, false));

        assert(IsSvsmMotionBenchmarkEvidenceValid(
            1000u, 1000u, 1000u, 0u, 1000u, 0u, false, false));
        assert(!IsSvsmMotionBenchmarkEvidenceValid(
            999u, 1000u, 1000u, 0u, 1000u, 0u, false, false));
        assert(!IsSvsmMotionBenchmarkEvidenceValid(
            1000u, 1000u, 999u, 1u, 999u, 0u, false, false));
        assert(!IsSvsmMotionBenchmarkEvidenceValid(
            1000u, 1000u, 1000u, 0u, 1000u, 1u, false, false));

        SparseVirtualShadowMapSettings original;
        const SparseVirtualShadowMapSettings identical = original;
        assert(IsSameSvsmConfiguration(original, identical));
        SparseVirtualShadowMapSettings changed = original;
        changed.tapCount = SvsmTapCount::Four;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.filterKernel = SvsmFilterKernel::NearestPoisson;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.poissonOrdering = SvsmPoissonOrdering::LegacyStride;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.packetStateSortingEnabled =
            !original.packetStateSortingEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.levelEmptyWorkSkipEnabled =
            !original.levelEmptyWorkSkipEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.finiteBudgetStaticDrainEnabled =
            !original.finiteBudgetStaticDrainEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.coarsestPageRenderBudgetEnabled = true;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.allocationBudgetSaturationEarlyOutEnabled =
            !original.allocationBudgetSaturationEarlyOutEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.perPixelMarkingDedupeEnabled =
            !original.perPixelMarkingDedupeEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.packetPageCullingEnabled =
            !original.packetPageCullingEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.hierarchicalScheduledPageMaskEnabled =
            !original.hierarchicalScheduledPageMaskEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.receiverPageMaskCullingEnabled =
            !original.receiverPageMaskCullingEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.staticDepthHierarchyCullingEnabled =
            !original.staticDepthHierarchyCullingEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.staticDepthHierarchyBias =
            original.staticDepthHierarchyBias + 0.001f;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.dirtyPageScatterRasterEnabled = true;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.scatterAlphaTestEarlyRejectEnabled = true;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.dirtyPageScatterAmplificationGuardEnabled = true;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.dirtyPageScatterMaximumAmplification = 8u;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.packetRectangleDirectScanEnabled =
            !original.packetRectangleDirectScanEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.recentPageEvictionGraceEnabled =
            !original.recentPageEvictionGraceEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.precomposedClipmapTransformsEnabled =
            !original.precomposedClipmapTransformsEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.renderPacketCachingEnabled =
            !original.renderPacketCachingEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.sharedClipmapPacketBuilderEnabled =
            !original.sharedClipmapPacketBuilderEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.persistentCasterSourceCachingEnabled =
            !original.persistentCasterSourceCachingEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.opaqueRasterSpecializationEnabled =
            !original.opaqueRasterSpecializationEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.leanAlphaTestedBindingsEnabled =
            !original.leanAlphaTestedBindingsEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.pairedStaticDynamicDepthEnabled =
            !original.pairedStaticDynamicDepthEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.deferredStaticDepthMergeEnabled =
            !original.deferredStaticDepthMergeEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.movingLightUncachedEnabled =
            !original.movingLightUncachedEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.retainPhysicalMappingsOnContentInvalidationEnabled =
            !original.
                retainPhysicalMappingsOnContentInvalidationEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.movingLightLodBiasEnabled =
            !original.movingLightLodBiasEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.movingLightResolutionBias =
            SvsmResolutionBias::PlusTwo;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.movingLightLodRecoveryFrames =
            original.movingLightLodRecoveryFrames + 1u;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.receiverDistanceMipClampEnabled =
            !original.receiverDistanceMipClampEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.receiverDistanceMipClampStartScale =
            original.receiverDistanceMipClampStartScale + 0.25f;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.receiverDistanceMipClampMaximumLevel =
            original.receiverDistanceMipClampMaximumLevel - 1u;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.movingLightContinuousReceiverBiasEnabled =
            !original.movingLightContinuousReceiverBiasEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.localizedInvalidationEnabled =
            !original.localizedInvalidationEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.tightLocalizedInvalidationBoundsEnabled =
            !original.tightLocalizedInvalidationBoundsEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.adaptiveCasterCacheClassificationEnabled =
            !original.adaptiveCasterCacheClassificationEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.defaultObjectInvalidationMode =
            SvsmObjectInvalidationMode::Rigid;
        assert(!IsSameSvsmConfiguration(original, changed));
        changed = original;
        changed.detailedGpuTimingEnabled =
            !original.detailedGpuTimingEnabled;
        assert(!IsSameSvsmConfiguration(original, changed));

        assert(ShouldUseSvsmOpaqueRasterSpecialization(
            true, true, true));
        assert(!ShouldUseSvsmOpaqueRasterSpecialization(
            false, true, true));
        assert(!ShouldUseSvsmOpaqueRasterSpecialization(
            true, false, true));
        // Alpha-tested and every other non-opaque domain retain the exact
        // material-bound reference path.
        assert(!ShouldUseSvsmOpaqueRasterSpecialization(
            true, true, false));
    }

    void TestResourceRecreationAndModeSwitch()
    {
        struct Key
        {
            uint32_t width;
            uint32_t height;
            uint32_t poolPages;

            bool operator==(const Key& other) const
            {
                return width == other.width &&
                    height == other.height &&
                    poolPages == other.poolPages;
            }
        };

        const Key original{ 1920u, 1080u, 4096u };
        assert(!(original == Key{ 1919u, 1080u, 4096u }));
        assert(!(original == Key{ 1920u, 1079u, 4096u }));
        assert(!(original == Key{ 1920u, 1080u, 2048u }));
        assert(original == Key{ 1920u, 1080u, 4096u });

        SvsmResourceBackend active = SvsmResourceBackend::None;
        auto activate = [&active](SvsmResourceBackend requested) {
            const bool recreate = RequiresSvsmResourceRecreation(
                active, requested);
            if (recreate)
                active = requested;
            return recreate;
        };

        assert(activate(SvsmResourceBackend::Dense));
        assert(!activate(SvsmResourceBackend::Dense));
        assert(activate(SvsmResourceBackend::Sparse));
        assert(!activate(SvsmResourceBackend::Sparse));
        assert(activate(SvsmResourceBackend::Dense));
        assert(active == SvsmResourceBackend::Dense);
    }

    void TestMotionBenchmarkAutostart()
    {
        assert(!IsSvsmMotionMeasurementMarkerReady(""));
        assert(!IsSvsmMotionMeasurementMarkerReady("state=waiting\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady("notstate=ready\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady("state=readyish\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady("state=ready\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady(
            "phase=measurement\nstate=ready\n"));

        const std::string readyMarker =
            "state=ready\r\n"
            "runIdentity=run-1\r\n"
            "monitorProcessId=10\r\n"
            "rendererProcessId=20\r\n"
            "rendererPath=C:/uvsr.exe\r\n"
            "measurementStartUnixMs=1000\r\n"
            "measurementDeadlineUnixMs=2000\r\n";
        assert(IsSvsmMotionMeasurementMarkerReady(readyMarker));
        assert(IsSvsmMotionMeasurementMarkerReady(
            std::string("\xef\xbb\xbf") + readyMarker));
        const SvsmMotionMeasurementMarker ready =
            ParseSvsmMotionMeasurementMarker(readyMarker);
        assert(ready.state ==
            SvsmMotionMeasurementMarkerState::Ready);
        assert(ready.identityValid);
        assert(ready.timingValid);
        assert(!ready.completionTimingValid);
        assert(IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 20u, "C:/uvsr.exe", 1000u));
        assert(IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 20u, "C:/uvsr.exe", 2000u));
        assert(!IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 21u, "C:/uvsr.exe", 1500u));
        assert(!IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 20u, "C:/other.exe", 1500u));
        assert(!IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 20u, "C:/uvsr.exe", 999u));
        assert(!IsSvsmMotionMeasurementMarkerReadyForRenderer(
            ready, 20u, "C:/uvsr.exe", 2001u));

        assert(!IsSvsmMotionMeasurementMarkerReady(
            "state=ready\n"
            "runIdentity=run-1\n"
            "monitorProcessId=10\n"
            "rendererProcessId=20\n"
            "rendererPath=C:/uvsr.exe\n"
            "measurementStartUtc=2026-07-22T00:00:00Z\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady(
            "state=ready\n"
            "runIdentity=run-1\n"
            "runIdentity=run-2\n"
            "monitorProcessId=10\n"
            "rendererProcessId=20\n"
            "rendererPath=C:/uvsr.exe\n"
            "measurementStartUnixMs=1000\n"
            "measurementDeadlineUnixMs=2000\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady(
            "state=ready\n"
            "runIdentity=run-1\n"
            "monitorProcessId=18446744073709551616\n"
            "rendererProcessId=20\n"
            "rendererPath=C:/uvsr.exe\n"
            "measurementStartUnixMs=1000\n"
            "measurementDeadlineUnixMs=2000\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady(
            "state=ready\n"
            "runIdentity=run-1\n"
            "monitorProcessId=10\n"
            "rendererProcessId=20\n"
            "rendererPath=C:/uvsr.exe\n"
            "measurementStartUnixMs=1000\n"
            "measurementDeadlineUnixMs=1000\n"));
        assert(!IsSvsmMotionMeasurementMarkerReady(
            readyMarker + "measurementEndUnixMs=2000\r\n"));

        const std::string completeMarker =
            "state=complete\n"
            "runIdentity=run-1\n"
            "monitorProcessId=10\n"
            "rendererProcessId=20\n"
            "rendererPath=C:/uvsr.exe\n"
            "measurementStartUnixMs=1000\n"
            "measurementDeadlineUnixMs=2000\n"
            "measurementEndUnixMs=2000\n";
        const SvsmMotionMeasurementMarker complete =
            ParseSvsmMotionMeasurementMarker(completeMarker);
        assert(complete.completionTimingValid);
        assert(IsSameSvsmMotionMeasurementRun(ready, complete));
        assert(IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, complete, 1999u));
        assert(IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, complete, 2000u));
        assert(!IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, complete, 2001u));

        const SvsmMotionMeasurementMarker earlyComplete =
            ParseSvsmMotionMeasurementMarker(
                "state=complete\n"
                "runIdentity=run-1\n"
                "monitorProcessId=10\n"
                "rendererProcessId=20\n"
                "rendererPath=C:/uvsr.exe\n"
                "measurementStartUnixMs=1000\n"
                "measurementDeadlineUnixMs=2000\n"
                "measurementEndUnixMs=1999\n");
        assert(!earlyComplete.completionTimingValid);
        assert(!IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, earlyComplete, 1500u));

        const SvsmMotionMeasurementMarker contaminated =
            ParseSvsmMotionMeasurementMarker(
                "state=contaminated\n"
                "runIdentity=run-1\n"
                "monitorProcessId=10\n"
                "rendererProcessId=20\n"
                "rendererPath=C:/uvsr.exe\n"
                "measurementStartUnixMs=1000\n"
                "measurementDeadlineUnixMs=2000\n"
                "measurementEndUnixMs=1500\n");
        assert(contaminated.completionTimingValid);
        assert(IsSameSvsmMotionMeasurementRun(ready, contaminated));
        assert(!IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, contaminated, 1400u));

        const SvsmMotionMeasurementMarker wrongRun =
            ParseSvsmMotionMeasurementMarker(
                "state=complete\n"
                "runIdentity=run-2\n"
                "monitorProcessId=10\n"
                "rendererProcessId=20\n"
                "rendererPath=C:/uvsr.exe\n"
                "measurementStartUnixMs=1000\n"
                "measurementDeadlineUnixMs=2000\n"
                "measurementEndUnixMs=2000\n");
        assert(!IsSameSvsmMotionMeasurementRun(ready, wrongRun));
        assert(!IsSvsmMotionMeasurementMarkerCleanCompletion(
            ready, wrongRun, 1500u));
        assert(IsSvsmMotionDiagnosticPoolPageCount(64u));
        assert(IsSvsmMotionDiagnosticPoolPageCount(256u));
        assert(IsSvsmMotionDiagnosticPoolPageCount(1024u));
        assert(IsSvsmMotionDiagnosticPoolPageCount(4096u));
        assert(!IsSvsmMotionDiagnosticPoolPageCount(0u));
        assert(!IsSvsmMotionDiagnosticPoolPageCount(65u));
        assert(IsSvsmMotionBenchmarkEnvironmentValid(false, false));
        assert(!IsSvsmMotionBenchmarkEnvironmentValid(true, false));
        assert(!IsSvsmMotionBenchmarkEnvironmentValid(false, true));
        assert(IsSvsmMotionBenchmarkAcceptanceConfiguration(
            4096u, 4u, false, true, true));
        assert(!IsSvsmMotionBenchmarkAcceptanceConfiguration(
            4096u, 3u, false, true, true));
        assert(!IsSvsmMotionBenchmarkAcceptanceConfiguration(
            4096u, 5u, false, true, true));
        assert(!IsSvsmMotionBenchmarkAcceptanceConfiguration(
            1024u, 4u, false, true, true));
        assert(!IsSvsmMotionBenchmarkAcceptanceConfiguration(
            4096u, 4u, false, false, true));
        assert(!IsSvsmMotionBenchmarkAcceptanceConfiguration(
            4096u, 4u, false, true, false));
        assert(!IsSvsmMotionBenchmarkAcceptanceConfiguration(
            4096u, 4u, true, true, true));

        assert(!IsSvsmMotionBenchmarkHierarchyRequested(
            false, false));
        assert(!IsSvsmMotionBenchmarkHierarchyRequested(
            false, true));
        assert(!IsSvsmMotionBenchmarkHierarchyRequested(
            true, false));
        assert(IsSvsmMotionBenchmarkHierarchyRequested(
            true, true));

        assert(IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
            false, false, false, false, false));
        assert(IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
            true, true, false, false, false));
        assert(IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
            true, false, false, true, false));
        assert(!IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
            true, false, false, true, true));
        assert(!IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
            true, false, false, false, false));
        assert(!IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
            true, true, true, true, false));
        assert(!IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
            false, false, true, true, false));

        SvsmMotionAutostartStage stage =
            SvsmMotionAutostartStage::Baseline;
        uint32_t stageFrames = 0u;
        uint32_t stableFrames = 0u;
        SvsmMotionAutostartDecision decision;
        for (uint32_t frame = 0u;
            frame <= SvsmMotionAutostartBaselineFrames;
            ++frame)
        {
            decision = AdvanceSvsmMotionAutostart(
                stage, stageFrames, stableFrames, false, false);
            stage = decision.stage;
            stageFrames = decision.stageFrames;
            stableFrames = decision.stableFrames;
            assert(decision.enableSvsm ==
                (frame == SvsmMotionAutostartBaselineFrames));
        }
        assert(stage == SvsmMotionAutostartStage::SvsmWarmup);

        decision = AdvanceSvsmMotionAutostart(
            stage, stageFrames, stableFrames, true, true);
        assert(decision.stableFrames == 0u);
        stageFrames = decision.stageFrames;
        stableFrames = decision.stableFrames;
        for (uint32_t frame = 0u;
            frame < SvsmMotionAutostartStableSvsmFrames;
            ++frame)
        {
            decision = AdvanceSvsmMotionAutostart(
                stage, stageFrames, stableFrames, true, false);
            stage = decision.stage;
            stageFrames = decision.stageFrames;
            stableFrames = decision.stableFrames;
            assert(decision.startBenchmark ==
                (frame + 1u == SvsmMotionAutostartStableSvsmFrames));
        }
        assert(stage == SvsmMotionAutostartStage::Ready);

        decision = AdvanceSvsmMotionAutostart(
            SvsmMotionAutostartStage::SvsmWarmup,
            SvsmMotionAutostartWarmupFrameLimit - 1u,
            0u,
            false,
            true);
        assert(decision.timedOut);
        assert(!decision.startBenchmark);
    }

    void TestTelemetrySampleOrdering()
    {
        assert(ShouldAcceptSvsmTelemetrySample(
            7u, 7u, 10u, 0u, false));
        assert(ShouldAcceptSvsmTelemetrySample(
            7u, 7u, 11u, 10u, true));
        assert(!ShouldAcceptSvsmTelemetrySample(
            6u, 7u, 12u, 10u, true));
        assert(!ShouldAcceptSvsmTelemetrySample(
            8u, 7u, 12u, 10u, true));
        assert(!ShouldAcceptSvsmTelemetrySample(
            7u, 7u, 10u, 10u, true));
        assert(!ShouldAcceptSvsmTelemetrySample(
            7u, 7u, 9u, 10u, true));

        assert(IsDetailedSvsmGpuTimingEnabled(true, false));
        assert(!IsDetailedSvsmGpuTimingEnabled(true, true));
        assert(!IsDetailedSvsmGpuTimingEnabled(false, false));
        assert(ShouldIssueSvsmGpuTimerStage(false, true));
        assert(!ShouldIssueSvsmGpuTimerStage(false, false));
        assert(ShouldIssueSvsmGpuTimerStage(true, false));

        assert(FirstSvsmStaticPageRequestRejectBit(0u) == 32u);
        assert(FirstSvsmStaticPageRequestRejectBit(1u) == 0u);
        assert(FirstSvsmStaticPageRequestRejectBit(1u << 22u) == 22u);
        assert(FirstSvsmStaticPageRequestRejectBit(
            (1u << 19u) | (1u << 3u)) == 3u);
    }

    void TestWorkTelemetrySurvivesNewerKnownZero()
    {
        SparseVirtualShadowMapTimings timings;
        SparseVirtualShadowMapGpuTiming work;
        work.pageMarkingMilliseconds = 0.125f;
        work.allocationMilliseconds = 0.25f;
        work.clearingMilliseconds = 0.5f;
        work.packetPageCullingMilliseconds = 0.75f;
        work.pageRenderingMilliseconds = 1.f;
        work.filteringMilliseconds = 1.25f;
        work.totalMilliseconds = 2.f;
        work.detailedGpuTimingEnabled = false;

        // This models a timer query from frame 40 retiring after frame 41
        // already published KnownZero. The primary UI must reject the older
        // sample, while independent work history must retain it.
        assert(!ShouldAcceptSvsmTelemetrySample(
            7u, 7u, 40u, 41u, true));
        assert(RetainSvsmCompletedUntaggedWorkTiming(
            timings, work, 40u, false));
        assert(timings.lastCompletedWorkTimingAvailable);
        assert(!timings.lastCompletedWorkDetailedGpuTimingEnabled);
        assert(timings.lastCompletedWorkSourceFrame == 40u);
        assert(timings.completedWorkSampleCount == 1u);
        assert(timings.lastCompletedWorkPageMarkingMilliseconds ==
            0.125f);
        assert(timings.lastCompletedWorkAllocationMilliseconds == 0.25f);
        assert(timings.lastCompletedWorkClearingMilliseconds == 0.5f);
        assert(
            timings.lastCompletedWorkPacketPageCullingMilliseconds ==
                0.75f);
        assert(timings.lastCompletedWorkPageRenderingMilliseconds == 1.f);
        assert(timings.lastCompletedWorkFilteringMilliseconds == 1.25f);
        assert(timings.lastCompletedWorkTotalMilliseconds == 2.f);

        SparseVirtualShadowMapGpuTiming tagged = work;
        tagged.sourceTag = 9u;
        assert(!RetainSvsmCompletedUntaggedWorkTiming(
            timings, tagged, 42u, false));
        assert(!RetainSvsmCompletedUntaggedWorkTiming(
            timings, work, 43u, true));
        assert(timings.lastCompletedWorkSourceFrame == 40u);
        assert(timings.completedWorkSampleCount == 1u);
    }

    void TestStaticZeroWorkObservabilityCounters()
    {
        SparseVirtualShadowMapTimings timings;
        for (uint32_t frame = 0u; frame < 1024u; ++frame)
            RecordSvsmStaticZeroWorkFrame(timings);

        assert(timings.gpuWorkSubmissionSerial == 0u);
        assert(timings.staticZeroWorkFrameStreak == 1024u);
        assert(timings.staticZeroWorkFrameTotal == 1024u);

        RecordSvsmGpuWorkSubmission(timings);
        assert(timings.gpuWorkSubmissionSerial == 1u);
        assert(timings.staticZeroWorkFrameStreak == 0u);
        assert(timings.staticZeroWorkFrameTotal == 1024u);

        RecordSvsmStaticZeroWorkFrame(timings);
        RecordSvsmStaticZeroWorkFrame(timings);
        assert(timings.gpuWorkSubmissionSerial == 1u);
        assert(timings.staticZeroWorkFrameStreak == 2u);
        assert(timings.staticZeroWorkFrameTotal == 1026u);

        RecordSvsmGpuWorkSubmission(timings);
        assert(timings.gpuWorkSubmissionSerial == 2u);
        assert(timings.staticZeroWorkFrameStreak == 0u);
        assert(timings.staticZeroWorkFrameTotal == 1026u);

        timings.gpuWorkSubmissionSerial =
            std::numeric_limits<uint64_t>::max();
        timings.staticZeroWorkFrameStreak =
            std::numeric_limits<uint64_t>::max();
        timings.staticZeroWorkFrameTotal =
            std::numeric_limits<uint64_t>::max();
        RecordSvsmGpuWorkSubmission(timings);
        RecordSvsmStaticZeroWorkFrame(timings);
        assert(timings.gpuWorkSubmissionSerial ==
            std::numeric_limits<uint64_t>::max());
        assert(timings.staticZeroWorkFrameStreak == 1u);
        assert(timings.staticZeroWorkFrameTotal ==
            std::numeric_limits<uint64_t>::max());
    }

    void TestPersistentCasterSourceCachePolicy()
    {
        const void* rootA =
            reinterpret_cast<const void*>(uintptr_t(0x1000u));
        const void* rootB =
            reinterpret_cast<const void*>(uintptr_t(0x2000u));
        SvsmPersistentCasterSourceKey key = {
            rootA, 7u, 0xabcdu, 23u, true
        };
        assert(IsSameSvsmPersistentCasterSourceKey(key, key));

        SvsmPersistentCasterSourceKey changed = key;
        changed.rootIdentity = rootB;
        assert(!IsSameSvsmPersistentCasterSourceKey(key, changed));
        changed = key;
        changed.sourceGeneration = 8u;
        assert(!IsSameSvsmPersistentCasterSourceKey(key, changed));
        changed = key;
        changed.casterStateHash ^= 1u;
        assert(!IsSameSvsmPersistentCasterSourceKey(key, changed));
        changed = key;
        changed.sourceRecordCount = 24u;
        assert(!IsSameSvsmPersistentCasterSourceKey(key, changed));
        changed = key;
        changed.reliable = false;
        assert(!IsSameSvsmPersistentCasterSourceKey(key, changed));

        // Light/view matrices are projection state and intentionally do not
        // participate in the stable source key.
        const uint32_t lightMatrixRevisionA = 11u;
        const uint32_t lightMatrixRevisionB = 12u;
        assert(lightMatrixRevisionA != lightMatrixRevisionB);
        assert(IsSameSvsmPersistentCasterSourceKey(key, key));

        assert(ShouldUseSvsmPersistentCasterSource(
            true, true, true, true, false));
        assert(!ShouldUseSvsmPersistentCasterSource(
            false, true, true, true, false));
        assert(!ShouldUseSvsmPersistentCasterSource(
            true, false, true, true, false));
        assert(!ShouldUseSvsmPersistentCasterSource(
            true, true, false, true, false));
        assert(!ShouldUseSvsmPersistentCasterSource(
            true, true, true, false, false));
        assert(!ShouldUseSvsmPersistentCasterSource(
            true, true, true, true, true));

        const SvsmCasterDirtyNodeDecision lightTransform =
            GetSvsmCasterDirtyNodeDecision(
                true, false, false, true, false, false);
        assert(!lightTransform.casterStateChanged);
        assert(lightTransform.inspectChildren);
        const SvsmCasterDirtyNodeDecision casterTransform =
            GetSvsmCasterDirtyNodeDecision(
                true, false, false, true, false, true);
        assert(casterTransform.casterStateChanged);
        assert(!casterTransform.inspectChildren);
        const SvsmCasterDirtyNodeDecision dirtyAncestor =
            GetSvsmCasterDirtyNodeDecision(
                false, false, false, true, false, true);
        assert(!dirtyAncestor.casterStateChanged);
        assert(dirtyAncestor.inspectChildren);
        const SvsmCasterDirtyNodeDecision structuralFailOpen =
            GetSvsmCasterDirtyNodeDecision(
                false, false, true, false, false, false);
        assert(structuralFailOpen.casterStateChanged);
        assert(!structuralFailOpen.inspectChildren);
        const SvsmCasterDirtyNodeDecision contentFailOpen =
            GetSvsmCasterDirtyNodeDecision(
                false, false, false, false, true, true);
        assert(contentFailOpen.casterStateChanged);

        assert(ShouldReuseSvsmAdaptiveCasterClassification(
            true, rootA, 7u, 23u, rootA, 7u, 23u));
        assert(!ShouldReuseSvsmAdaptiveCasterClassification(
            false, rootA, 7u, 23u, rootA, 7u, 23u));
        assert(!ShouldReuseSvsmAdaptiveCasterClassification(
            true, rootA, 7u, 23u, rootB, 7u, 23u));
        assert(!ShouldReuseSvsmAdaptiveCasterClassification(
            true, rootA, 7u, 23u, rootA, 8u, 23u));
        assert(!ShouldReuseSvsmAdaptiveCasterClassification(
            true, rootA, 7u, 23u, rootA, 7u, 24u));
        assert(GetNextSvsmPacketClassificationGeneration(
            std::numeric_limits<uint64_t>::max()) == 1u);

        SvsmAffineSignatureParts affine;
        affine.linear = {
            1.f, 2.f, 3.f,
            4.f, 5.f, 6.f,
            7.f, 8.f, 9.f
        };
        affine.translation = { 10.f, 11.f, 12.f };
        const std::array<float, 12u> encoded =
            MakeSvsmAffineSignature(affine);
        SvsmAffineSignatureParts decoded;
        assert(DecodeSvsmAffineSignature(encoded, decoded));
        assert(decoded.linear == affine.linear);
        assert(decoded.translation == affine.translation);
        std::array<float, 12u> invalidAffine = encoded;
        invalidAffine[4] =
            std::numeric_limits<float>::quiet_NaN();
        assert(!DecodeSvsmAffineSignature(
            invalidAffine, decoded));

        const SvsmCopiedBoundsSignature bounds = {
            { -1.f, -2.f, -3.f },
            { 4.f, 5.f, 6.f }
        };
        assert(!HasSvsmCopiedBoundsChanged(bounds, bounds));
        SvsmCopiedBoundsSignature mutatedBounds = bounds;
        mutatedBounds.maximum[1] = 5.25f;
        assert(HasSvsmCopiedBoundsChanged(
            bounds, mutatedBounds));

        SparseVirtualShadowMapSettings original;
        SparseVirtualShadowMapSettings toggleChanged = original;
        toggleChanged.persistentCasterSourceCachingEnabled =
            !original.persistentCasterSourceCachingEnabled;
        assert(!IsSameSvsmConfiguration(
            original, toggleChanged));
        toggleChanged = original;
        toggleChanged.casterOnlySceneRevisionEnabled =
            !original.casterOnlySceneRevisionEnabled;
        assert(!IsSameSvsmConfiguration(
            original, toggleChanged));
    }
}

int main()
{
    TestPerObjectInvalidationResolver();
    TestPagePacking();
    TestPairedStaticDynamicDepth();
    TestDeferredStaticDepthMerge();
    TestMovingLightCachePolicy();
    TestReceiverDistanceMipClamp();
    TestFinePageRenderBudgetScheduling();
    TestLightDepthOriginGuardBand();
    TestMappingAndWraparound();
    TestPerPixelRequestDeduplication();
    TestPacketPageCompaction();
    TestTightLocalizedInvalidationProjection();
    TestScheduledPageTileMaskHierarchy();
    TestStaticDepthPageHierarchy();
    TestReceiverPageMaskHierarchy();
    TestStaticPageRequestActions();
    TestClipmapSelection();
    TestReverseDepthWrites();
    TestAllocationEvictionAndCacheReuse();
    TestInvalidationAndFallback();
    TestPageBoundaryFiltering();
    TestPoissonOrdering();
    TestProfiles();
    TestPersistentCasterSourceCachePolicy();
    TestAlphaTestedRasterSpecialization();
    TestTapCountPermutationSelection();
    TestCachedShadowDrawListPolicy();
    TestBatchedDrawPackingAndGrouping();
    TestMotionBenchmarkSequence();
    TestMotionBenchmarkAutostart();
    TestResourceRecreationAndModeSwitch();
    TestTelemetrySampleOrdering();
    TestWorkTelemetrySurvivesNewerKnownZero();
    TestStaticZeroWorkObservabilityCounters();
    return 0;
}
