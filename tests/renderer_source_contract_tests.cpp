#include "msaa_visibility_resolve_contract.h"
#include "renderer_producer_contract.h"
#include "renderer_scene_load_worker.h"
#include "renderer_scene_retirement.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::cerr << "Renderer contract validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const char* message)
    {
        if (!condition)
            Fail(message);
    }

    void TestEveryProducerCombination()
    {
        for (std::uint32_t bits = 0u; bits < 256u; ++bits)
        {
            const uvsr::RendererProducerDispatchContract contract{
                bool(bits & (1u << 0u)),
                bool(bits & (1u << 1u)),
                bool(bits & (1u << 2u)),
                bool(bits & (1u << 3u)),
                bool(bits & (1u << 4u)),
                bool(bits & (1u << 5u)),
                bool(bits & (1u << 6u)),
                bool(bits & (1u << 7u))
            };
            const bool expected =
                (!(bits & (1u << 0u)) || (bits & (1u << 1u))) &&
                (!(bits & (1u << 2u)) || (bits & (1u << 3u))) &&
                (!(bits & (1u << 4u)) || (bits & (1u << 5u))) &&
                (!(bits & (1u << 6u)) || (bits & (1u << 7u)));
            Require(contract.IsComplete() == expected,
                "producer completion truth table changed");
        }
    }

    void TestPreparedTransactionCancellation()
    {
        int cancellationCount = 0;
        bool prepared = true;
        {
            auto cancellation =
                uvsr::MakePreparedRendererTransactionCancellation(
                    prepared,
                    [&]() noexcept { ++cancellationCount; });
            (void)cancellation;
        }
        Require(!prepared && cancellationCount == 1,
            "early exit did not cancel exactly once");

        prepared = false;
        {
            auto cancellation =
                uvsr::MakePreparedRendererTransactionCancellation(
                    prepared,
                    [&]() noexcept { ++cancellationCount; });
            (void)cancellation;
        }
        Require(cancellationCount == 1,
            "completed transaction was cancelled");

        prepared = true;
        {
            auto cancellation =
                uvsr::MakePreparedRendererTransactionCancellation(
                    prepared,
                    [&]() noexcept { ++cancellationCount; });
            prepared = false;
            (void)cancellation;
        }
        Require(cancellationCount == 1,
            "committed transaction was cancelled");
    }

    void TestMsaaResolvePublicationIsAtomic()
    {
        std::array<int, uvsr::MsaaVisibilityResolveResourceCount> storage{};
        uvsr::MsaaVisibilityResolveOutputs candidate;
        nvrhi::ITexture** outputs[] = {
            &candidate.depth,
            &candidate.diffuse,
            &candidate.material,
            &candidate.normals,
            &candidate.emissive,
            &candidate.materialAmbientOcclusion,
            &candidate.motionVectors
        };
        for (std::size_t index = 0u; index < storage.size(); ++index)
        {
            *outputs[index] = reinterpret_cast<nvrhi::ITexture*>(
                &storage[index]);
        }

        uvsr::MsaaVisibilityResolveOutputs published = candidate;
        Require(!uvsr::PublishMsaaVisibilityResolveOutputs(
                false, candidate, published),
            "failed dispatch published MSAA resolve outputs");
        for (nvrhi::ITexture* texture :
            uvsr::GetMsaaVisibilityResolveOutputTextures(published))
        {
            Require(texture == nullptr,
                "failed dispatch retained a partial MSAA output");
        }

        candidate.normals = nullptr;
        Require(!uvsr::PublishMsaaVisibilityResolveOutputs(
                true, candidate, published),
            "incomplete output set was published");
        candidate.normals = reinterpret_cast<nvrhi::ITexture*>(&storage[3]);
        Require(uvsr::PublishMsaaVisibilityResolveOutputs(
                true, candidate, published),
            "complete output set was rejected");
        Require(published.motionVectors == candidate.motionVectors,
            "complete output set was not committed atomically");
    }

    void TestRetirementPrecedesWorkerPublication()
    {
        int pollCount = 0;
        uvsr::RendererSceneRetirementOperations operations;
        operations.armQuery = [] { return true; };
        operations.pollQuery = [&]
        {
            return ++pollCount == 1
                ? uvsr::RendererSceneQueryStatus::Pending
                : uvsr::RendererSceneQueryStatus::Complete;
        };
        operations.waitForIdle = [] { return false; };
        uvsr::RendererSceneRetirement retirement(std::move(operations));
        Require(retirement.Begin(), "scene retirement did not arm");
        Require(retirement.Poll() ==
                uvsr::RendererSceneRetirementStatus::Pending,
            "retirement skipped its arm boundary");
        Require(retirement.Poll() ==
                uvsr::RendererSceneRetirementStatus::Pending,
            "pending query published retirement");
        Require(retirement.Poll() ==
                uvsr::RendererSceneRetirementStatus::Ready,
            "completed query did not publish retirement");
        Require(retirement.Consume(), "ready retirement was not consumed");

        int publishedCpuState = 0;
        uvsr::RendererSceneLoadWorker worker;
        Require(worker.Start([&]
            {
                publishedCpuState = 42;
                return true;
            }),
            "replacement scene worker did not start");
        Require(worker.Join() && publishedCpuState == 42,
            "replacement CPU handoff did not publish after retirement");
    }
}

int main()
{
    TestEveryProducerCombination();
    TestPreparedTransactionCancellation();
    TestMsaaResolvePublicationIsAtomic();
    TestRetirementPrecedesWorkerPublication();
    std::cout << "UVSR renderer direct contracts passed\n";
    return EXIT_SUCCESS;
}
