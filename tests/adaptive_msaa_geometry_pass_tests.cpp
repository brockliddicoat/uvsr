#include "../src/adaptive_msaa_geometry_pass.h"

#include <iostream>

namespace
{
    int failures = 0;

    void Check(bool condition, const char* message)
    {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

int main()
{
    using uvsr::AdaptiveMsaaRasterSubmissionTrace;

    const AdaptiveMsaaRasterSubmissionTrace reference;
    AdaptiveMsaaRasterSubmissionTrace replay = reference;
    Check(reference.Matches(replay),
        "identical diagnostic replay traces match");

    replay.submittedTriangles = 9u;
    Check(!reference.Matches(replay),
        "triangle-count changes reject diagnostic publication");

    replay = reference;
    replay.submittedDraws = 1u;
    Check(!reference.Matches(replay),
        "draw-count changes reject equal-triangle diagnostic publication");

    replay = reference;
    replay.sequenceHash ^= 1u;
    Check(!reference.Matches(replay),
        "different draw identities reject equal aggregate publication");

    AdaptiveMsaaRasterSubmissionTrace firstRecord;
    firstRecord.submittedDraws = 1u;
    firstRecord.records.resize(1u);
    firstRecord.records[0].geometryLocalIndex = 3u;
    AdaptiveMsaaRasterSubmissionTrace differentRecord = firstRecord;
    differentRecord.records[0].geometryLocalIndex = 4u;
    Check(!firstRecord.Matches(differentRecord),
        "record identity rejects a theoretical equal-hash collision");

    AdaptiveMsaaRasterSubmissionTrace firstView;
    firstView.views.push_back(
        reinterpret_cast<const donut::engine::IView*>(1u));
    AdaptiveMsaaRasterSubmissionTrace differentView = firstView;
    differentView.views[0] =
        reinterpret_cast<const donut::engine::IView*>(2u);
    Check(!firstView.Matches(differentView),
        "view identity rejects a theoretical equal-hash collision");

    if (failures != 0)
        return 1;
    std::cout << "Adaptive MSAA geometry-pass tests passed.\n";
    return 0;
}
