#include "renderer_texture_bmp.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Renderer BMP test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    std::uint32_t ReadU32(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset)
    {
        Require(offset + 4u <= bytes.size(), "truncated u32 field");
        return std::uint32_t(bytes[offset]) |
            (std::uint32_t(bytes[offset + 1u]) << 8u) |
            (std::uint32_t(bytes[offset + 2u]) << 16u) |
            (std::uint32_t(bytes[offset + 3u]) << 24u);
    }
}

int main(int argc, char** argv)
{
    Require(argc == 2, "expected one external scratch-directory argument");
    const std::filesystem::path directory = argv[1];
    std::filesystem::create_directories(directory);
    const std::filesystem::path outputPath = directory / "known-answer.bmp";

    constexpr std::size_t SourcePitch = 12u;
    const std::array<std::uint8_t, SourcePitch * 2u> rgba = {
        0x10u, 0x20u, 0x30u, 0x40u,
        0x50u, 0x60u, 0x70u, 0x80u,
        0xeeu, 0xeeu, 0xeeu, 0xeeu,
        0x90u, 0xa0u, 0xb0u, 0xc0u,
        0xd0u, 0xe0u, 0xf0u, 0xffu,
        0xddu, 0xddu, 0xddu, 0xddu
    };
    Require(
        uvsr::WriteRendererBmp(
            outputPath, 2u, 2u, SourcePitch, rgba.data()),
        "known-answer image was not written");

    std::ifstream input(outputPath, std::ios::binary);
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    constexpr std::array<std::uint8_t, 16> ExpectedPixels = {
        0xb0u, 0xa0u, 0x90u, 0xc0u,
        0xf0u, 0xe0u, 0xd0u, 0xffu,
        0x30u, 0x20u, 0x10u, 0x40u,
        0x70u, 0x60u, 0x50u, 0x80u
    };
    Require(bytes.size() == 70u, "known-answer file size changed");
    Require(bytes[0] == 'B' && bytes[1] == 'M', "BMP signature changed");
    Require(ReadU32(bytes, 2u) == bytes.size(), "BMP file size changed");
    Require(ReadU32(bytes, 10u) == 54u, "BMP pixel offset changed");
    Require(ReadU32(bytes, 18u) == 2u, "BMP width changed");
    Require(ReadU32(bytes, 22u) == 2u, "BMP height changed");
    Require(ReadU32(bytes, 34u) == ExpectedPixels.size(),
        "BMP payload size changed");
    Require(std::equal(
        ExpectedPixels.begin(),
        ExpectedPixels.end(),
        bytes.begin() + 54),
        "RGBA-to-bottom-up-BGRA conversion changed");

    const std::filesystem::path invalidPath = directory / "invalid.bmp";
    Require(
        !uvsr::WriteRendererBmp(invalidPath, 0u, 2u, SourcePitch, rgba.data()) &&
        !uvsr::WriteRendererBmp(invalidPath, 2u, 0u, SourcePitch, rgba.data()) &&
        !uvsr::WriteRendererBmp(invalidPath, 2u, 2u, 7u, rgba.data()) &&
        !uvsr::WriteRendererBmp(invalidPath, 2u, 2u, SourcePitch, nullptr) &&
        !std::filesystem::exists(invalidPath),
        "invalid input published an artifact");
    Require(
        !uvsr::WriteRendererBmp(
            directory / "missing" / "failure.bmp",
            2u,
            2u,
            SourcePitch,
            rgba.data()),
        "unwritable destination was reported as successful");
    return 0;
}
