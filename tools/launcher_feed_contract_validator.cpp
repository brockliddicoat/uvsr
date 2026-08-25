#include "strict_json_contract.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace uvsr::contract;

    constexpr std::string_view ProductId =
        "0c47a7a8-1ec4-4ffd-b6c4-2f7614181223";
    constexpr std::string_view KeyId =
        "uvsr-launcher-update-p256-2026-01";
    constexpr std::int64_t MaximumReleaseSequence = 9007199254740991ll;
    constexpr std::int64_t MaximumLauncherBytes = 256ll * 1024ll * 1024ll;
    constexpr std::int64_t MaximumRendererBytes =
        32ll * 1024ll * 1024ll * 1024ll;

    [[nodiscard]] bool IsStableVersion(std::string_view value)
    {
        unsigned parts = 0u;
        std::size_t position = 0u;
        while (position < value.size())
        {
            const std::size_t begin = position;
            while (position < value.size() && value[position] >= '0' &&
                value[position] <= '9')
            {
                ++position;
            }
            if (begin == position ||
                (value[begin] == '0' && position - begin != 1u))
            {
                return false;
            }
            std::int64_t part = 0;
            const auto parsed = std::from_chars(
                value.data() + begin, value.data() + position, part);
            if (parsed.ec != std::errc{} ||
                part > std::numeric_limits<std::int32_t>::max())
            {
                return false;
            }
            ++parts;
            if (position == value.size())
                break;
            if (value[position++] != '.')
                return false;
        }
        return parts == 3u;
    }

    [[nodiscard]] bool IsEngineVersion(std::string_view value)
    {
        unsigned parts = 0u;
        std::size_t position = 0u;
        while (position < value.size())
        {
            const std::size_t begin = position;
            while (position < value.size() && value[position] >= '0' &&
                value[position] <= '9')
            {
                ++position;
            }
            if (begin == position ||
                (value[begin] == '0' && position - begin != 1u))
            {
                return false;
            }
            std::int64_t part = 0;
            const auto parsed = std::from_chars(
                value.data() + begin, value.data() + position, part);
            if (parsed.ec != std::errc{} || part > 65535)
                return false;
            ++parts;
            if (position == value.size())
                break;
            if (value[position++] != '.')
                return false;
        }
        return parts == 4u;
    }

    [[nodiscard]] JsonValue DecodeEnvelope(
        std::string_view text,
        std::int64_t schemaVersion)
    {
        const JsonValue envelope = ParseJson(text);
        RequireExactObject(envelope,
            { "schemaVersion", "keyId", "payloadBase64", "signatureBase64" },
            "feed envelope");
        if (Integer(Member(envelope, "schemaVersion"), "envelope schema") !=
            schemaVersion)
        {
            throw std::runtime_error("feed envelope schema is not canonical");
        }
        if (String(Member(envelope, "keyId"), "feed key") != KeyId)
            throw std::runtime_error("feed key is not canonical");
        const std::string& payloadText =
            String(Member(envelope, "payloadBase64"), "payload");
        const std::string& signatureText =
            String(Member(envelope, "signatureBase64"), "signature");
        const std::vector<unsigned char> payload = DecodeBase64(payloadText);
        const std::vector<unsigned char> signature = DecodeBase64(signatureText);
        if (payload.empty() || payload.size() > 8u * 1024u ||
            signature.size() != 64u)
        {
            throw std::runtime_error("feed cryptographic fields exceed limits");
        }
        return ParseJson(std::string_view(
            reinterpret_cast<const char*>(payload.data()), payload.size()));
    }

    void ValidateArtifact(
        const JsonValue& artifact,
        std::string_view requiredName,
        std::int64_t maximumBytes)
    {
        RequireExactObject(artifact, { "name", "size", "sha256" },
            "feed artifact");
        if (String(Member(artifact, "name"), "artifact name") != requiredName)
            throw std::runtime_error("artifact name is not canonical");
        const std::int64_t size = Integer(Member(artifact, "size"),
            "artifact size");
        if (size < 1 || size > maximumBytes)
            throw std::runtime_error("artifact size is outside its limit");
        if (!IsLowerHex(String(Member(artifact, "sha256"),
                "artifact SHA-256"), 64u))
        {
            throw std::runtime_error("artifact SHA-256 is not canonical");
        }
    }

    void ValidateCommon(
        const JsonValue& payload,
        std::int64_t schemaVersion)
    {
        if (Integer(Member(payload, "schemaVersion"), "payload schema") !=
            schemaVersion ||
            String(Member(payload, "productId"), "product ID") != ProductId ||
            String(Member(payload, "channel"), "channel") != "stable")
        {
            throw std::runtime_error("feed identity is not canonical");
        }
        const std::int64_t sequence = Integer(
            Member(payload, "releaseSequence"), "release sequence");
        if (sequence < 1 || sequence > MaximumReleaseSequence)
            throw std::runtime_error("release sequence is outside its limit");
        if (!IsLowerHex(String(Member(payload, "sourceCommit"),
                "source commit"), 40u))
        {
            throw std::runtime_error("source commit is not canonical");
        }
    }

    void ValidateLauncher(std::string_view text)
    {
        const JsonValue payload = DecodeEnvelope(text, 2);
        RequireExactObject(payload,
            { "schemaVersion", "productId", "channel", "releaseSequence",
              "version", "sourceCommit", "artifact" },
            "launcher feed payload");
        ValidateCommon(payload, 2);
        if (!IsStableVersion(String(Member(payload, "version"),
                "launcher version")))
        {
            throw std::runtime_error("launcher version is not canonical");
        }
        ValidateArtifact(Member(payload, "artifact"),
            "uvsr-launcher.exe", MaximumLauncherBytes);
    }

    void ValidateRenderer(std::string_view text)
    {
        const JsonValue payload = DecodeEnvelope(text, 1);
        RequireExactObject(payload,
            { "schemaVersion", "productId", "channel", "releaseSequence",
              "sourceCommit", "settingsHash", "engineVersion", "artifact" },
            "renderer feed payload");
        ValidateCommon(payload, 1);
        const std::string& settingsHash = String(
            Member(payload, "settingsHash"), "settings hash");
        if (!IsLowerHex(settingsHash, 32u))
            throw std::runtime_error("settings hash is not canonical");
        if (!IsEngineVersion(String(
                Member(payload, "engineVersion"), "engine version")))
            throw std::runtime_error("engine version is not canonical");
        ValidateArtifact(Member(payload, "artifact"),
            "uvsr-renderer-windows-11-x64.zip", MaximumRendererBytes);
    }

    [[nodiscard]] std::string Envelope(std::string_view payload, int schema)
    {
        const std::vector<unsigned char> signature(64u, 0x5au);
        return "{\"schemaVersion\":" + std::to_string(schema) +
            ",\"keyId\":\"" + std::string(KeyId) +
            "\",\"payloadBase64\":\"" + EncodeBase64(payload) +
            "\",\"signatureBase64\":\"" + EncodeBase64(signature) + "\"}";
    }

    void RequireFailure(const std::function<void()>& operation)
    {
        try
        {
            operation();
        }
        catch (const std::exception&)
        {
            return;
        }
        throw std::runtime_error("invalid feed fixture was accepted");
    }

    void SelfTest()
    {
        const std::string commit(40u, 'a');
        const std::string sha(64u, 'b');
        const std::string settings = "9c50b0f1515e89d856c8ebb627b86984";
        const std::string engineVersion = "40016.45297.20830.35288";
        const std::string launcherPayload =
            "{\"schemaVersion\":2,\"productId\":\"" +
            std::string(ProductId) +
            "\",\"channel\":\"stable\",\"releaseSequence\":16," +
            "\"version\":\"1.2.0\",\"sourceCommit\":\"" + commit +
            "\",\"artifact\":{\"name\":\"uvsr-launcher.exe\"," +
            "\"size\":123,\"sha256\":\"" + sha + "\"}}";
        const std::string rendererPayload =
            "{\"schemaVersion\":1,\"productId\":\"" +
            std::string(ProductId) +
            "\",\"channel\":\"stable\",\"releaseSequence\":16," +
            "\"sourceCommit\":\"" + commit +
            "\",\"settingsHash\":\"" + settings +
            "\",\"engineVersion\":\"" + engineVersion +
            "\",\"artifact\":{\"name\":" +
            "\"uvsr-renderer-windows-11-x64.zip\",\"size\":456," +
            "\"sha256\":\"" + sha + "\"}}";
        ValidateLauncher(Envelope(launcherPayload, 2));
        ValidateRenderer(Envelope(rendererPayload, 1));

        RequireFailure([&]
        {
            ValidateLauncher(Envelope(
                launcherPayload + " ", 1));
        });
        RequireFailure([&]
        {
            const std::string duplicate =
                "{\"schemaVersion\":2,\"schemaVersion\":2," +
                launcherPayload.substr(1u);
            ValidateLauncher(Envelope(duplicate, 2));
        });
        RequireFailure([&]
        {
            std::string invalid = launcherPayload;
            invalid.replace(invalid.find("1.2.0"), 5u, "01.2.0");
            ValidateLauncher(Envelope(invalid, 2));
        });
        RequireFailure([&]
        {
            std::string invalid = rendererPayload;
            invalid.replace(invalid.find(engineVersion),
                engineVersion.size(), "01.2.3.4");
            ValidateRenderer(Envelope(invalid, 1));
        });
        RequireFailure([&]
        {
            std::string invalid = Envelope(rendererPayload, 1);
            const std::size_t payload = invalid.find("payloadBase64");
            const std::size_t value = invalid.find('"', payload + 15u) + 1u;
            invalid[value] = '!';
            ValidateRenderer(invalid);
        });
    }
}

int main(int argumentCount, char** arguments)
{
    try
    {
        if (argumentCount == 2 &&
            std::string_view(arguments[1]) == "--self-test")
        {
            SelfTest();
            std::cout << "launcher feed contract self-test passed\n";
            return EXIT_SUCCESS;
        }
        if (argumentCount == 3)
        {
            const std::string text = ReadFile(arguments[2], 16u * 1024u);
            if (std::string_view(arguments[1]) == "--check-launcher")
                ValidateLauncher(text);
            else if (std::string_view(arguments[1]) == "--check-renderer")
                ValidateRenderer(text);
            else
                throw std::runtime_error("unknown feed kind");
            std::cout << "feed schema and values passed; signature verification "
                "remains the trusted launcher's responsibility\n";
            return EXIT_SUCCESS;
        }
        std::cerr << "usage: uvsr_launcher_feed_contract_validator "
            "--check-launcher <feed> | --check-renderer <feed> | --self-test\n";
        return EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        std::cerr << "launcher feed contract failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
