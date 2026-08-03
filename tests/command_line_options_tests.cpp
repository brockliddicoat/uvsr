#include "command_line_options.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    void Require(bool condition)
    {
        if (!condition)
        {
            std::cerr << "Command-line option validation failed\n";
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using namespace donut::app;

    int signedValue = -1;
    Require(ParseCommandLineInt("1", 1, 4096, signedValue));
    Require(signedValue == 1);
    Require(ParseCommandLineInt("4096", 1, 4096, signedValue));
    Require(signedValue == 4096);
    Require(!ParseCommandLineInt("", 1, 4096, signedValue));
    Require(!ParseCommandLineInt("0", 1, 4096, signedValue));
    Require(!ParseCommandLineInt("-1", 0, 4096, signedValue));
    Require(!ParseCommandLineInt("42px", 1, 4096, signedValue));
    Require(!ParseCommandLineInt("999999999999999999999", 1,
        std::numeric_limits<int>::max(), signedValue));
    return 0;
}
