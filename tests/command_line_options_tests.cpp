#include "command_line_options.h"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

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

    constexpr std::array<std::string_view, 12> DonutApiOptions = {
        "-d3d11",
        "-dx11",
        "--d3d11",
        "--dx11",
        "-d3d12",
        "-dx12",
        "--d3d12",
        "--dx12",
        "-vk",
        "-vulkan",
        "--vk",
        "--vulkan"
    };
    for (const std::string_view option : DonutApiOptions)
        Require(IsDonutGraphicsApiOption(option));
    Require(!IsDonutGraphicsApiOption("--d3d"));
    Require(!IsDonutGraphicsApiOption("--svsm-motion-test"));
    Require(!IsDonutGraphicsApiOption("scene.json"));

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

    uint32_t unsignedValue = 1u;
    Require(ParseCommandLineUint32("0", unsignedValue));
    Require(unsignedValue == 0u);
    Require(ParseCommandLineUint32("4294967295", unsignedValue));
    Require(unsignedValue == std::numeric_limits<uint32_t>::max());
    Require(!ParseCommandLineUint32("", unsignedValue));
    Require(!ParseCommandLineUint32("-1", unsignedValue));
    Require(!ParseCommandLineUint32("+1", unsignedValue));
    Require(!ParseCommandLineUint32("1.0", unsignedValue));
    Require(!ParseCommandLineUint32("4294967296", unsignedValue));

    return 0;
}
