#include "experiment_title.h"

#include <cstdlib>
#include <iostream>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Experiment-title validation failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    Require(uvsr::IsValidExperimentTitle("main"),
        "main must remain valid");
    Require(uvsr::IsValidExperimentTitle("nightneutral"),
        "lowercase ASCII words must remain valid");

    Require(!uvsr::IsValidExperimentTitle(""),
        "an empty title must be rejected");
    Require(!uvsr::IsValidExperimentTitle("NightNeutral"),
        "mixed-case titles must be rejected");
    Require(!uvsr::IsValidExperimentTitle("NIGHT"),
        "uppercase titles must be rejected");
    Require(!uvsr::IsValidExperimentTitle("night2"),
        "digits must be rejected");
    Require(!uvsr::IsValidExperimentTitle("night-neutral"),
        "punctuation must be rejected");
    Require(!uvsr::IsValidExperimentTitle("night\n"),
        "trailing newlines must be rejected");

    std::cout << "UVSR experiment-title validation passed\n";
    return EXIT_SUCCESS;
}
