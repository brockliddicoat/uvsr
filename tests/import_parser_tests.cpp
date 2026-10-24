#include <fastgltf/core.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if SIMDJSON_EXCEPTIONS != 0 || FASTGLTF_USE_64BIT_FLOAT != 0 || FASTGLTF_USE_CUSTOM_SMALLVECTOR != 0
#error unexpected parser configuration
#endif

namespace
{
    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "import parser check failed: %s\n", reason);
        exit(1);
    }

    fastgltf::Expected<fastgltf::Asset> Parse(fastgltf::Parser& parser, const char* json)
    {
        auto data = fastgltf::GltfDataBuffer::FromBytes(
            reinterpret_cast<const std::byte*>(json), strlen(json));
        Require(data.error() == fastgltf::Error::None, "input bytes");
        return parser.loadGltfJson(data.get(), {});
    }
}

int main()
{
    fastgltf::Asset owned;
    {
        fastgltf::Parser parser;
        auto parsed = Parse(parser, R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[0]}],"scene":0,"nodes":[{"name":"owned after parser exit","translation":[1,2,3]}]})");
        Require(parsed.error() == fastgltf::Error::None && parsed.get().nodes.size() == 1, "valid scene");
        owned = std::move(parsed.get());
        const struct { const char* json; fastgltf::Error expected; } rejected[]{
            {"{ invalid json", fastgltf::Error::InvalidJson},
            {R"({"nodes":[]})", fastgltf::Error::InvalidOrMissingAssetField},
            {R"({"asset":{"version":"1.0"}})", fastgltf::Error::UnsupportedVersion},
            {R"({"asset":{"version":"2.0"},"extensionsRequired":["UVSR_unknown_required"]})", fastgltf::Error::UnknownRequiredExtension},
            {R"({"asset":{"version":"2.0"},"extensionsRequired":["KHR_lights_punctual"]})", fastgltf::Error::MissingExtensions}
        };
        for (const auto& input : rejected)
            Require(Parse(parser,input.json).error() == input.expected, "explicit parser failure");
        Require(Parse(parser,R"({"asset":{"version":"2.0"},"nodes":[{}]})").error() == fastgltf::Error::None,
            "parser reuse after failures");
    }
    Require(owned.nodes.size() == 1 && owned.nodes[0].name == "owned after parser exit" &&
        std::get<fastgltf::TRS>(owned.nodes[0].transform).translation[2] == 3,
        "asset storage survives input bytes and parser destruction");
    puts("import parser: C++17 no-exceptions, owned asset, five explicit failures and parser reuse passed");
    return 0;
}
