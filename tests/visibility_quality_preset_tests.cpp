#include "visibility_quality_preset.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    using namespace uvsr;

    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Visibility quality preset validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    void RequirePreset(
        ScreenSpaceVisibilityQuality quality,
        VisibilitySpatialFilter expectedFilter,
        const char* name)
    {
        const VisibilityQualityReconstructionPreset preset =
            GetVisibilityQualityReconstructionPreset(quality);
        Require(preset.spatialEnabled,
            std::string(name) + " must enable spatial reconstruction");
        Require(preset.spatialFilter == expectedFilter,
            std::string(name) + " must select its exact reconstruction");
        Require(preset.spatialRadius == 4.0f,
            std::string(name) + " must retain the validated radius 4");
    }
}

int main()
{
    RequirePreset(
        ScreenSpaceVisibilityQuality::Low,
        VisibilitySpatialFilter::JointBilateral,
        "Low");
    RequirePreset(
        ScreenSpaceVisibilityQuality::Medium,
        VisibilitySpatialFilter::JointBilateral,
        "Medium");
    RequirePreset(
        ScreenSpaceVisibilityQuality::High,
        VisibilitySpatialFilter::GaussianJointBilateral,
        "High");
    RequirePreset(
        ScreenSpaceVisibilityQuality::Ultra,
        VisibilitySpatialFilter::GaussianJointBilateral,
        "Ultra");

    const VisibilityQualityReconstructionPreset custom =
        GetVisibilityQualityReconstructionPreset(
            ScreenSpaceVisibilityQuality::Custom);
    Require(!custom.spatialEnabled,
        "Custom fallback must not silently enable reconstruction");
    Require(
        custom.spatialFilter ==
            VisibilitySpatialFilter::GaussianJointBilateral &&
        custom.spatialRadius == 4.0f,
        "Custom fallback must retain safe dormant Gaussian defaults");

    std::cout << "UVSR visibility quality presets passed.\n";
    return 0;
}
