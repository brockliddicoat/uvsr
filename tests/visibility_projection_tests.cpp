#include "visibility_projection_cpu.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Visibility projection validation failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    bool Near(float actual, float expected, float tolerance = 2e-6f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    float Inset(float scale)
    {
        return scale * (1.f - VisibilityProjectionClipInset);
    }

    void TestPerspectiveForwardNearClip()
    {
        VisibilityProjectionClipResult result =
            ComputeVisibilityProjectionEndpointClip(
                0.25f, 1.f, -0.75f, 1.f, false);
        Require(result.valid != 0u && result.clipped != 0u,
            "forward perspective endpoint clips at z=0");
        Require(Near(result.endpointScale, Inset(0.25f)),
            "forward perspective near-plane parameter is analytic");
        float clippedZ = 0.25f + (-0.75f - 0.25f) * result.endpointScale;
        Require(clippedZ >= 0.f,
            "forward endpoint remains inside the near plane after inset");
    }

    void TestPerspectiveReverseNearClip()
    {
        VisibilityProjectionClipResult result =
            ComputeVisibilityProjectionEndpointClip(
                0.75f, 1.f, 1.75f, 1.f, true);
        Require(result.valid != 0u && result.clipped != 0u,
            "reverse perspective endpoint clips at z=w");
        Require(Near(result.endpointScale, Inset(0.25f)),
            "reverse perspective near-plane parameter is analytic");
        float clippedZ = 0.75f + (1.75f - 0.75f) * result.endpointScale;
        Require(1.f - clippedZ >= 0.f,
            "reverse endpoint remains inside the near plane after inset");
    }

    void TestPositiveWClip()
    {
        VisibilityProjectionClipResult result =
            ComputeVisibilityProjectionEndpointClip(
                0.5f, 1.f, 0.5f, -1.f, false);
        const float expected = Inset((1.f - 2.f * VisibilityProjectionEpsilon) / 2.f);
        Require(result.valid != 0u && result.clipped != 0u,
            "camera-plane crossing clips to positive homogeneous w");
        Require(Near(result.endpointScale, expected),
            "positive-w clipping parameter is analytic");
        float clippedW = 1.f + (-1.f - 1.f) * result.endpointScale;
        Require(clippedW > VisibilityProjectionEpsilon,
            "clipped perspective endpoint has a safe divisor");
    }

    void TestOrthographicPathsRemainUnchanged()
    {
        VisibilityProjectionClipResult forward =
            ComputeVisibilityProjectionEndpointClip(
                0.2f, 1.f, 0.2f, 1.f, false);
        VisibilityProjectionClipResult reverse =
            ComputeVisibilityProjectionEndpointClip(
                0.8f, 1.f, 0.8f, 1.f, true);
        Require(forward.valid != 0u && forward.clipped == 0u &&
                Near(forward.endpointScale, 1.f),
            "orthographic forward endpoint is unchanged");
        Require(reverse.valid != 0u && reverse.clipped == 0u &&
                Near(reverse.endpointScale, 1.f),
            "orthographic reverse endpoint is unchanged");
    }

    void TestNearPlaneAndLargeRadiusLimits()
    {
        VisibilityProjectionClipResult onNearPlane =
            ComputeVisibilityProjectionEndpointClip(
                0.f, 1.f, -1.f, 1.f, false);
        Require(onNearPlane.valid != 0u && onNearPlane.clipped != 0u &&
                Near(onNearPlane.endpointScale, 0.f),
            "receiver on the near plane clips a crossing endpoint to itself");

        VisibilityProjectionClipResult negativeSide =
            ComputeVisibilityProjectionEndpointClip(
                0.1f, 1.f, -1000.f, 1.f, false);
        VisibilityProjectionClipResult positiveSide =
            ComputeVisibilityProjectionEndpointClip(
                0.1f, 1.f, -1000.f, 1.f, false);
        Require(negativeSide.valid != 0u && positiveSide.valid != 0u,
            "very large finite radius remains clip-valid on both horizon sides");
        Require(Near(negativeSide.endpointScale, positiveSide.endpointScale),
            "symmetric horizon sides use the same homogeneous clip parameter");
        Require(negativeSide.endpointScale > 0.f &&
                negativeSide.endpointScale < 0.001f,
            "large-radius crossing shortens analytically without iteration");
    }

    void TestDegenerateClipSpansAreRejected()
    {
        // A crossing endpoint is only reconstructed by dividing the receiver-
        // to-endpoint span. When the receiver sits just inside the valid domain
        // and the endpoint lands just past the boundary, that span collapses
        // below the epsilon guard. The clip must reject rather than divide by a
        // near-zero denominator and emit a point on the projection singularity.

        // Positive-w domain: receiver w barely exceeds epsilon and the endpoint
        // w falls at/below it, leaving receiverClipW - endpointClipW < epsilon.
        VisibilityProjectionClipResult positiveW =
            ComputeVisibilityProjectionEndpointClip(
                0.f, 1.4e-6f, 0.f, 0.5e-6f, false);
        Require(positiveW.valid == 0u,
            "an unspannable positive-w crossing is rejected, not divided by ~0");

        // Near plane: receiver rests exactly on the plane and the endpoint only
        // just crosses it, so receiverNearDistance - endpointNearDistance stays
        // under the epsilon guard on both depth conventions.
        VisibilityProjectionClipResult forwardNear =
            ComputeVisibilityProjectionEndpointClip(
                0.f, 1.f, -0.5e-6f, 1.f, false);
        Require(forwardNear.valid == 0u,
            "an unspannable forward near-plane crossing is rejected");
        VisibilityProjectionClipResult reverseNear =
            ComputeVisibilityProjectionEndpointClip(
                1.f, 1.f, 1.f + 0.5e-6f, 1.f, true);
        Require(reverseNear.valid == 0u,
            "an unspannable reverse near-plane crossing is rejected");
    }

    void TestForwardReverseSymmetryAndInvalidInputs()
    {
        VisibilityProjectionClipResult forward =
            ComputeVisibilityProjectionEndpointClip(
                0.3f, 1.f, -0.7f, 1.f, false);
        // z_reverse = w - z_forward preserves the same near-plane distances.
        VisibilityProjectionClipResult reverse =
            ComputeVisibilityProjectionEndpointClip(
                0.7f, 1.f, 1.7f, 1.f, true);
        Require(forward.valid != 0u && reverse.valid != 0u &&
                Near(forward.endpointScale, reverse.endpointScale),
            "forward and reversed depth conventions produce symmetric clipping");

        Require(ComputeVisibilityProjectionEndpointClip(
                0.2f, 0.f, 0.2f, 1.f, false).valid == 0u,
            "receiver with non-positive w is rejected");
        Require(ComputeVisibilityProjectionEndpointClip(
                -0.1f, 1.f, 0.2f, 1.f, false).valid == 0u,
            "receiver outside the active near plane is rejected");
        Require(ComputeVisibilityProjectionEndpointClip(
                0.2f, 1.f,
                std::numeric_limits<float>::infinity(), 1.f, false).valid == 0u,
            "non-finite endpoint is rejected");
    }
}

int main()
{
    TestPerspectiveForwardNearClip();
    TestPerspectiveReverseNearClip();
    TestPositiveWClip();
    TestOrthographicPathsRemainUnchanged();
    TestNearPlaneAndLargeRadiusLimits();
    TestDegenerateClipSpansAreRejected();
    TestForwardReverseSymmetryAndInvalidInputs();

    std::cout << "UVSR visibility projection validation passed\n";
    return EXIT_SUCCESS;
}
