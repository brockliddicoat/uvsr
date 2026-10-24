#include <fastgltf/core.hpp>
#include <stdio.h>

int main()
{
    const char valid[] = R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[0]}],"scene":0,"nodes":[{"name":"probe"}]})";
    auto bytes = fastgltf::GltfDataBuffer::FromBytes(
        reinterpret_cast<const std::byte*>(valid), sizeof(valid) - 1);
    if (bytes.error() != fastgltf::Error::None)
        return 1;
    fastgltf::Parser parser;
    auto asset = parser.loadGltfJson(bytes.get(), {});
    if (asset.error() != fastgltf::Error::None || asset.get().nodes.size() != 1)
        return 2;
    const char malformed[] = "{ definitely not json";
    auto invalid = fastgltf::GltfDataBuffer::FromBytes(
        reinterpret_cast<const std::byte*>(malformed), sizeof(malformed) - 1);
    if (invalid.error() != fastgltf::Error::None)
        return 3;
    auto rejected = parser.loadGltfJson(invalid.get(), {});
    if (rejected.error() != fastgltf::Error::InvalidJson)
        return 4;
    puts("C++17 no-exception importer: one node and checked InvalidJson passed");
    return 0;
}
