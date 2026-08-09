#include "noise_settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr double Pi = 3.1415926535897932384626433832795;

    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Visibility sampling validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    bool Near(double left, double right, double tolerance = 1e-9)
    {
        return std::abs(left - right) <= tolerance;
    }

    enum class ReferenceTimingStage : uint32_t
    {
        FirstTrace,
        Reconstruction,
        Composition,
        EffectEnvelope,
        Count
    };

    struct ReferenceTimingSnapshot
    {
        float firstTraceMs = 0.f;
        float reconstructionMs = 0.f;
        float compositionMs = 0.f;
        float effectEnvelopeMs = 0.f;
        bool available = false;
    };

    struct ReferenceTimingSlot
    {
        uint32_t submittedStageMask = 0u;
        uint32_t resolvedStageMask = 0u;
        std::array<float,
            static_cast<size_t>(ReferenceTimingStage::Count)>
            resolvedStageMilliseconds{};
    };

    uint32_t ReferenceTimingStageMask(ReferenceTimingStage stage)
    {
        return 1u << static_cast<uint32_t>(stage);
    }

    bool SameTimingSnapshot(
        const ReferenceTimingSnapshot& left,
        const ReferenceTimingSnapshot& right)
    {
        return left.firstTraceMs == right.firstTraceMs &&
            left.reconstructionMs == right.reconstructionMs &&
            left.compositionMs == right.compositionMs &&
            left.effectEnvelopeMs == right.effectEnvelopeMs &&
            left.available == right.available;
    }

    bool TimingSlotWritable(const ReferenceTimingSlot& slot)
    {
        return slot.submittedStageMask == 0u &&
            slot.resolvedStageMask == 0u;
    }

    void SubmitReferenceTimingStage(
        ReferenceTimingSlot& slot,
        ReferenceTimingStage stage)
    {
        const uint32_t stageMask = ReferenceTimingStageMask(stage);
        Require((slot.submittedStageMask & stageMask) == 0u,
            "a timing stage is submitted only once per latency slot");
        slot.submittedStageMask |= stageMask;
    }

    bool ResolveReferenceTimingStage(
        ReferenceTimingSlot& slot,
        ReferenceTimingStage stage,
        float milliseconds,
        ReferenceTimingSnapshot& published)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        const uint32_t stageMask = 1u << stageIndex;
        Require((slot.submittedStageMask & stageMask) != 0u &&
                (slot.resolvedStageMask & stageMask) == 0u,
            "only a submitted unresolved timing stage can resolve");
        slot.resolvedStageMilliseconds[stageIndex] = milliseconds;
        slot.resolvedStageMask |= stageMask;
        if (slot.resolvedStageMask != slot.submittedStageMask)
            return false;

        const auto millisecondsOrZero = [&slot](ReferenceTimingStage value)
        {
            const uint32_t valueIndex = static_cast<uint32_t>(value);
            const uint32_t valueMask = 1u << valueIndex;
            return (slot.submittedStageMask & valueMask) != 0u
                ? slot.resolvedStageMilliseconds[valueIndex]
                : 0.f;
        };
        published = {
            millisecondsOrZero(ReferenceTimingStage::FirstTrace),
            millisecondsOrZero(ReferenceTimingStage::Reconstruction),
            millisecondsOrZero(ReferenceTimingStage::Composition),
            millisecondsOrZero(ReferenceTimingStage::EffectEnvelope),
            true
        };
        slot = {};
        return true;
    }

    void ValidateAtomicVisibilityTimingReference()
    {
        ReferenceTimingSnapshot published = {
            91.f, 92.f, 93.f, 94.f, true
        };
        ReferenceTimingSlot slot;
        SubmitReferenceTimingStage(slot, ReferenceTimingStage::FirstTrace);
        SubmitReferenceTimingStage(slot, ReferenceTimingStage::Reconstruction);
        SubmitReferenceTimingStage(slot, ReferenceTimingStage::Composition);
        SubmitReferenceTimingStage(slot, ReferenceTimingStage::EffectEnvelope);
        const ReferenceTimingSnapshot original = published;

        Require(!ResolveReferenceTimingStage(slot,
                    ReferenceTimingStage::Reconstruction, 2.f, published) &&
                !ResolveReferenceTimingStage(slot,
                    ReferenceTimingStage::EffectEnvelope, 8.f, published) &&
                !ResolveReferenceTimingStage(slot,
                    ReferenceTimingStage::FirstTrace, 1.f, published) &&
                SameTimingSnapshot(published, original) &&
                !TimingSlotWritable(slot),
            "staggered queries retain the prior snapshot until all resolve");
        Require(ResolveReferenceTimingStage(slot,
                    ReferenceTimingStage::Composition, 3.f, published) &&
                published.firstTraceMs == 1.f &&
                published.reconstructionMs == 2.f &&
                published.compositionMs == 3.f &&
                published.effectEnvelopeMs == 8.f &&
                TimingSlotWritable(slot),
            "a completed slot publishes one coherent frame and then retires");

        SubmitReferenceTimingStage(slot, ReferenceTimingStage::FirstTrace);
        SubmitReferenceTimingStage(slot, ReferenceTimingStage::Composition);
        SubmitReferenceTimingStage(slot, ReferenceTimingStage::EffectEnvelope);
        const ReferenceTimingSnapshot reconstructedFrame = published;
        Require(!ResolveReferenceTimingStage(slot,
                    ReferenceTimingStage::EffectEnvelope, 7.f, published) &&
                !ResolveReferenceTimingStage(slot,
                    ReferenceTimingStage::Composition, 4.f, published) &&
                SameTimingSnapshot(published, reconstructedFrame) &&
                !TimingSlotWritable(slot),
            "an early envelope result cannot publish a partial later frame");
        Require(ResolveReferenceTimingStage(slot,
                    ReferenceTimingStage::FirstTrace, 2.f, published) &&
                published.firstTraceMs == 2.f &&
                published.reconstructionMs == 0.f &&
                published.compositionMs == 4.f &&
                published.effectEnvelopeMs == 7.f &&
                TimingSlotWritable(slot),
            "an omitted reconstruction stage publishes zero without stale cost");
    }

    struct NoiseAddress
    {
        std::array<uint32_t, 2> coordinate{};
        uint32_t layer = 0u;

        bool operator==(const NoiseAddress& other) const
        {
            return coordinate == other.coordinate &&
                layer == other.layer;
        }

        bool operator!=(const NoiseAddress& other) const
        {
            return !(*this == other);
        }
    };

    uint32_t NoiseHash(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    std::array<uint32_t, 2> NoiseStreamOffset(uint32_t semanticStream)
    {
        return {
            NoiseHash(semanticStream ^ 0x68bc21ebu),
            NoiseHash(semanticStream ^ 0x02e5be93u)
        };
    }

    std::array<uint32_t, 2> NoiseSpatialPhaseOffset(uint32_t phase)
    {
        return {
            phase * 0xc13fa9a9u,
            phase * 0x91e10da5u
        };
    }

    std::array<uint32_t, 2> NoiseCenteredCoordinate(
        std::array<uint32_t, 2> localPixel,
        std::array<uint32_t, 2> localExtent,
        uint32_t resolution,
        std::array<uint32_t, 2> translation)
    {
        const int64_t centeredX = int64_t(localPixel[0]) -
            int64_t(localExtent[0] / 2u) + int64_t(resolution / 2u);
        const int64_t centeredY = int64_t(localPixel[1]) -
            int64_t(localExtent[1] / 2u) + int64_t(resolution / 2u);
        const uint32_t mask = resolution - 1u;
        return {
            (uint32_t(centeredX) + translation[0]) & mask,
            (uint32_t(centeredY) + translation[1]) & mask
        };
    }

    NoiseAddress ResolveNoiseAddress(
        uvsr::NoisePattern pattern,
        std::array<uint32_t, 2> localPixel,
        std::array<uint32_t, 2> localExtent,
        uint32_t resolution,
        uint32_t layers,
        uint32_t phase,
        uint32_t semanticStream)
    {
        std::array<uint32_t, 2> translation =
            NoiseStreamOffset(semanticStream);
        uint32_t layer = 0u;
        if (pattern == uvsr::NoisePattern::SpatiotemporalBlue)
        {
            layer = phase & (layers - 1u);
        }
        else
        {
            const std::array<uint32_t, 2> phaseOffset =
                NoiseSpatialPhaseOffset(phase);
            translation[0] += phaseOffset[0];
            translation[1] += phaseOffset[1];
        }
        return {
            NoiseCenteredCoordinate(
                localPixel, localExtent, resolution, translation),
            layer
        };
    }

    double DecodeR8Noise(uint8_t value)
    {
        return (double(value) + 0.5) / 256.0;
    }

    void ValidateNoiseSettingsContract()
    {
        using namespace uvsr;

        static_assert(static_cast<uint32_t>(NoisePattern::SpatialWhite) == 0u);
        static_assert(static_cast<uint32_t>(NoisePattern::SpatialBlue) == 1u);
        static_assert(
            static_cast<uint32_t>(NoisePattern::SpatiotemporalBlue) == 2u);
        static_assert(static_cast<uint32_t>(NoisePattern::Count) == 3u);

        const NoiseSettings defaults;
        Require(defaults.pattern == NoisePattern::SpatiotemporalBlue &&
                defaults.resolution == NoiseResolution::Size128 &&
                defaults.animate,
            "global noise defaults to animated 128x128x64 ST Blue");
        const NoiseOverrideSettings defaultOverride;
        Require(!defaultOverride.specifyNoise &&
                defaultOverride.custom == defaults,
            "effect noise inherits globally while retaining custom defaults");

        constexpr std::array<NoisePattern, 3> patterns = {
            NoisePattern::SpatialWhite,
            NoisePattern::SpatialBlue,
            NoisePattern::SpatiotemporalBlue
        };
        constexpr std::array<const char*, 3> patternLabels = {
            "Spatial White", "Spatial Blue", "Spatiotemporal Blue"
        };
        constexpr std::array<uint32_t, 3> layerCounts = { 1u, 1u, 64u };
        for (size_t index = 0u; index < patterns.size(); ++index)
        {
            Require(IsValidNoisePattern(patterns[index]),
                "every public noise pattern is valid");
            Require(std::string(GetNoisePatternLabel(patterns[index])) ==
                    patternLabels[index],
                "noise pattern labels match the public UI contract");
            Require(GetNoiseLayerCount(patterns[index]) ==
                    layerCounts[index],
                "noise patterns expose their exact array depth");
        }
        Require(!IsValidNoisePattern(NoisePattern::Count) &&
                !IsValidNoisePattern(static_cast<NoisePattern>(-1)) &&
                GetNoiseLayerCount(NoisePattern::Count) == 0u &&
                std::string(GetNoisePatternLabel(NoisePattern::Count)).empty(),
            "invalid noise patterns fail closed");

        constexpr std::array<NoiseResolution, 4> resolutions = {
            NoiseResolution::Size64,
            NoiseResolution::Size128,
            NoiseResolution::Size256,
            NoiseResolution::Size512
        };
        constexpr std::array<uint32_t, 4> resolutionValues = {
            64u, 128u, 256u, 512u
        };
        constexpr std::array<const char*, 4> resolutionLabels = {
            "64x64", "128x128", "256x256", "512x512"
        };
        for (size_t index = 0u; index < resolutions.size(); ++index)
        {
            Require(IsValidNoiseResolution(resolutions[index]) &&
                    GetNoiseResolutionValue(resolutions[index]) ==
                        resolutionValues[index] &&
                    std::string(GetNoiseResolutionLabel(
                        resolutions[index])) == resolutionLabels[index],
                "noise resolution values and labels stay exact");
        }
        const NoiseResolution invalidResolution =
            static_cast<NoiseResolution>(1024u);
        Require(!IsValidNoiseResolution(invalidResolution) &&
                GetNoiseResolutionValue(invalidResolution) == 0u &&
                std::string(GetNoiseResolutionLabel(
                    invalidResolution)).empty(),
            "invalid noise resolutions fail closed");

        NoiseSettings global;
        global.pattern = NoisePattern::SpatialWhite;
        global.resolution = NoiseResolution::Size512;
        global.animate = false;
        NoiseOverrideSettings effect;
        effect.custom.pattern = NoisePattern::SpatialBlue;
        effect.custom.resolution = NoiseResolution::Size64;
        effect.custom.animate = true;
        Require(ResolveNoiseSettings(global, effect) == global,
            "a disabled effect override inherits every global noise field");
        effect.specifyNoise = true;
        Require(ResolveNoiseSettings(global, effect) == effect.custom &&
                ResolveNoiseSettings(global, effect) != global,
            "Specify Noise isolates the selected effect configuration");

        NoiseSettings invalid = defaults;
        invalid.pattern = NoisePattern::Count;
        Require(!IsValidNoiseSettings(invalid),
            "settings reject an invalid pattern");
        invalid = defaults;
        invalid.resolution = invalidResolution;
        Require(!IsValidNoiseSettings(invalid),
            "settings reject an invalid resolution");
    }

    void ValidateCenteredSamplingContract()
    {
        using namespace uvsr;

        constexpr uint32_t resolution = 128u;
        const std::array<uint32_t, 2> noTranslation = { 0u, 0u };
        Require(NoiseCenteredCoordinate(
                { 960u, 540u },
                { 1920u, 1080u },
                resolution,
                noTranslation) == std::array<uint32_t, 2>{ 64u, 64u },
            "the local dispatch center maps to the texture center");

        const std::array<uint32_t, 2> translation = { 91u, 37u };
        const auto wide = NoiseCenteredCoordinate(
            { 960u + 17u, 540u - 9u },
            { 1920u, 1080u },
            resolution,
            translation);
        const auto narrow = NoiseCenteredCoordinate(
            { 640u + 17u, 360u - 9u },
            { 1280u, 720u },
            resolution,
            translation);
        Require(wide == narrow,
            "equal center-relative pixels survive edge cropping unchanged");
        Require(NoiseCenteredCoordinate(
                { 401u, 211u }, { 1024u, 768u }, resolution, translation) ==
                NoiseCenteredCoordinate(
                    { 401u + resolution, 211u },
                    { 1024u, 768u },
                    resolution,
                    translation),
            "centered noise coordinates tile toroidally");

        const NoiseAddress stPhase11 = ResolveNoiseAddress(
            NoisePattern::SpatiotemporalBlue,
            { 17u, 29u },
            { 320u, 180u },
            resolution,
            64u,
            11u,
            3u);
        const NoiseAddress stPhase12 = ResolveNoiseAddress(
            NoisePattern::SpatiotemporalBlue,
            { 17u, 29u },
            { 320u, 180u },
            resolution,
            64u,
            12u,
            3u);
        Require(stPhase11.coordinate == stPhase12.coordinate &&
                stPhase11.layer == 11u && stPhase12.layer == 12u,
            "ST Blue advances only through its temporal array slices");
        Require(ResolveNoiseAddress(
                NoisePattern::SpatiotemporalBlue,
                { 17u, 29u }, { 320u, 180u },
                resolution, 64u, 75u, 3u) == stPhase11,
            "ST Blue wraps its 64-slice sequence with a layer mask");

        const NoiseAddress stOtherStream = ResolveNoiseAddress(
            NoisePattern::SpatiotemporalBlue,
            { 17u, 29u },
            { 320u, 180u },
            resolution,
            64u,
            11u,
            4u);
        Require(stOtherStream.coordinate != stPhase11.coordinate &&
                stOtherStream.layer == stPhase11.layer,
            "semantic streams use fixed, disjoint XY offsets");

        const NoiseAddress spatialFrozen = ResolveNoiseAddress(
            NoisePattern::SpatialBlue,
            { 17u, 29u },
            { 320u, 180u },
            resolution,
            1u,
            0u,
            3u);
        const NoiseAddress spatialAdvanced = ResolveNoiseAddress(
            NoisePattern::SpatialBlue,
            { 17u, 29u },
            { 320u, 180u },
            resolution,
            1u,
            1u,
            3u);
        Require(spatialFrozen.layer == 0u &&
                spatialAdvanced.layer == 0u &&
                spatialFrozen.coordinate != spatialAdvanced.coordinate,
            "spatial noise animates by deterministic toroidal translation");
        Require(ResolveNoiseAddress(
                NoisePattern::SpatialBlue,
                { 17u, 29u }, { 320u, 180u },
                resolution, 1u, 0u, 3u) == spatialFrozen,
            "phase zero is the stable frozen-animation address");

        double previous = 0.0;
        for (uint32_t encoded = 0u; encoded <= 255u; ++encoded)
        {
            const double decoded = DecodeR8Noise(uint8_t(encoded));
            Require(decoded > 0.0 && decoded < 1.0,
                "R8 noise decodes inside the open unit interval");
            if (encoded != 0u)
            {
                Require(Near(decoded - previous, 1.0 / 256.0),
                    "adjacent R8 bins decode to uniform bin centers");
            }
            previous = decoded;
        }
        Require(Near(DecodeR8Noise(0u), 0.5 / 256.0) &&
                Near(DecodeR8Noise(255u), 255.5 / 256.0),
            "R8 endpoint bins decode to their exact open centers");
    }

    uint32_t RotateRight(uint32_t value, uint32_t shift)
    {
        return (value >> shift) | (value << (32u - shift));
    }

    std::string Sha256(const std::vector<uint8_t>& source)
    {
        constexpr std::array<uint32_t, 64> roundConstants = {{
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        }};
        std::array<uint32_t, 8> state = {{
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
        }};

        std::vector<uint8_t> padded = source;
        const uint64_t bitLength = uint64_t(source.size()) * 8u;
        padded.push_back(0x80u);
        while ((padded.size() + 8u) % 64u != 0u)
            padded.push_back(0u);
        for (int shift = 56; shift >= 0; shift -= 8)
            padded.push_back(uint8_t(bitLength >> uint32_t(shift)));

        for (size_t offset = 0u; offset < padded.size(); offset += 64u)
        {
            std::array<uint32_t, 64> words{};
            for (uint32_t index = 0u; index < 16u; ++index)
            {
                const size_t byte = offset + size_t(index) * 4u;
                words[index] =
                    (uint32_t(padded[byte]) << 24u) |
                    (uint32_t(padded[byte + 1u]) << 16u) |
                    (uint32_t(padded[byte + 2u]) << 8u) |
                    uint32_t(padded[byte + 3u]);
            }
            for (uint32_t index = 16u; index < 64u; ++index)
            {
                const uint32_t first =
                    RotateRight(words[index - 15u], 7u) ^
                    RotateRight(words[index - 15u], 18u) ^
                    (words[index - 15u] >> 3u);
                const uint32_t second =
                    RotateRight(words[index - 2u], 17u) ^
                    RotateRight(words[index - 2u], 19u) ^
                    (words[index - 2u] >> 10u);
                words[index] = words[index - 16u] + first +
                    words[index - 7u] + second;
            }

            uint32_t a = state[0];
            uint32_t b = state[1];
            uint32_t c = state[2];
            uint32_t d = state[3];
            uint32_t e = state[4];
            uint32_t f = state[5];
            uint32_t g = state[6];
            uint32_t h = state[7];
            for (uint32_t index = 0u; index < 64u; ++index)
            {
                const uint32_t upper =
                    RotateRight(e, 6u) ^ RotateRight(e, 11u) ^
                    RotateRight(e, 25u);
                const uint32_t choose = (e & f) ^ (~e & g);
                const uint32_t first = h + upper + choose +
                    roundConstants[index] + words[index];
                const uint32_t lower =
                    RotateRight(a, 2u) ^ RotateRight(a, 13u) ^
                    RotateRight(a, 22u);
                const uint32_t majority =
                    (a & b) ^ (a & c) ^ (b & c);
                const uint32_t second = lower + majority;
                h = g;
                g = f;
                f = e;
                e = d + first;
                d = c;
                c = b;
                b = a;
                a = first + second;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }

        std::ostringstream result;
        result << std::hex << std::setfill('0');
        for (uint32_t word : state)
            result << std::setw(8) << word;
        return result.str();
    }

    std::vector<uint8_t> ReadBinary(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        Require(bool(input), "noise asset opens: " + path.string());
        const std::streamoff size =
            static_cast<std::streamoff>(input.tellg());
        Require(size >= 0, "noise asset size is readable: " + path.string());
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        input.seekg(0, std::ios::beg);
        Require(bool(input.read(
                reinterpret_cast<char*>(bytes.data()), size)),
            "noise asset payload is complete: " + path.string());
        return bytes;
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        Require(bool(input), "text file opens: " + path.string());
        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }

    std::filesystem::path FindRepositoryRoot()
    {
        std::array<std::filesystem::path, 2> starts = {
            std::filesystem::absolute(std::filesystem::path(__FILE__)),
            std::filesystem::current_path()
        };
        for (std::filesystem::path start : starts)
        {
            if (std::filesystem::is_regular_file(start))
                start = start.parent_path();
            while (!start.empty())
            {
                if (std::filesystem::is_regular_file(
                        start / "assets/noise/manifest.json"))
                {
                    return start;
                }
                const std::filesystem::path parent = start.parent_path();
                if (parent == start)
                    break;
                start = parent;
            }
        }
        Fail("could not locate assets/noise/manifest.json");
    }

    struct ExpectedNoiseAsset
    {
        uvsr::NoisePattern pattern;
        uvsr::NoiseResolution resolution;
        const char* fileName;
        const char* sha256;
    };

    constexpr std::array<ExpectedNoiseAsset, 12> ExpectedNoiseAssets = {{
        { uvsr::NoisePattern::SpatialWhite, uvsr::NoiseResolution::Size64,
            "spatial-white-64x64x1-r8.bin",
            "fe4cb771dcf6631e45d10e416794abc6cb263143eb9b66626651994aa5125de8" },
        { uvsr::NoisePattern::SpatialBlue, uvsr::NoiseResolution::Size64,
            "spatial-blue-64x64x1-r8.bin",
            "88d47915ec8a00a1e0e806440e91ee2c20db7aa22794eee8016e9fda30013e46" },
        { uvsr::NoisePattern::SpatiotemporalBlue, uvsr::NoiseResolution::Size64,
            "spatiotemporal-blue-64x64x64-r8.bin",
            "c637a502c36359aeb0193d718a00ec286b0fe1c8499fb173631fad58f7c6c7fc" },
        { uvsr::NoisePattern::SpatialWhite, uvsr::NoiseResolution::Size128,
            "spatial-white-128x128x1-r8.bin",
            "753043935f1cb58d35e4cf651e11397a7b1e18fa99296f627d2597f20ca2cc22" },
        { uvsr::NoisePattern::SpatialBlue, uvsr::NoiseResolution::Size128,
            "spatial-blue-128x128x1-r8.bin",
            "f0c18c9d5869eb6d5afabecf7a3969efa41be7378e0429d1ddd8747bbb4d8ff1" },
        { uvsr::NoisePattern::SpatiotemporalBlue, uvsr::NoiseResolution::Size128,
            "spatiotemporal-blue-128x128x64-r8.bin",
            "bed4f4bf7705885db4af2becf8409cf688042d6709cbd8092ce0b316a64b63dc" },
        { uvsr::NoisePattern::SpatialWhite, uvsr::NoiseResolution::Size256,
            "spatial-white-256x256x1-r8.bin",
            "e7e03f79f879fed6dac81b0dd4735ffc0c46f8c76ef0d89b4eb9ea4058625932" },
        { uvsr::NoisePattern::SpatialBlue, uvsr::NoiseResolution::Size256,
            "spatial-blue-256x256x1-r8.bin",
            "c7eb79c2217d79da5a670bea361ab07a04f450babf7ea043c18557690052c972" },
        { uvsr::NoisePattern::SpatiotemporalBlue, uvsr::NoiseResolution::Size256,
            "spatiotemporal-blue-256x256x64-r8.bin",
            "da89bc55d2b825ee6d27899a45f1678ed99637c1527e5228eea4d4896c3056ae" },
        { uvsr::NoisePattern::SpatialWhite, uvsr::NoiseResolution::Size512,
            "spatial-white-512x512x1-r8.bin",
            "3672e6338bdf1ac366387f0db8d336361dac292007b2b14da0c3ae5fba260ab5" },
        { uvsr::NoisePattern::SpatialBlue, uvsr::NoiseResolution::Size512,
            "spatial-blue-512x512x1-r8.bin",
            "5d2618fd124f1c73a56d73a7ad45418c08e4b6d032896fd2b1f11b029aa9c4fb" },
        { uvsr::NoisePattern::SpatiotemporalBlue, uvsr::NoiseResolution::Size512,
            "spatiotemporal-blue-512x512x64-r8.bin",
            "c4292f0e2d3d57d49334bdfe665b4aeff8ddf86ca101a6cb5364dbf2fb00a703" }
    }};

    size_t CountOccurrences(
        const std::string& text,
        const std::string& token)
    {
        size_t count = 0u;
        size_t position = 0u;
        while ((position = text.find(token, position)) != std::string::npos)
        {
            ++count;
            position += token.size();
        }
        return count;
    }

    void RequireContains(
        const std::string& text,
        const std::string& token,
        const std::string& message)
    {
        Require(text.find(token) != std::string::npos, message);
    }

    void ValidateAtomicVisibilityTimingSourceContract()
    {
        const std::filesystem::path sourceDirectory =
            FindRepositoryRoot() / "src";
        const std::string header = ReadText(
            sourceDirectory / "screen_space_visibility.h");
        const std::string source = ReadText(
            sourceDirectory / "screen_space_visibility.cpp");

        RequireContains(header, "struct TimerSlot",
            "visibility timing keeps explicit per-slot state");
        RequireContains(header, "submittedStageMask",
            "visibility timing records every submitted stage per slot");
        RequireContains(header, "resolvedStageMask",
            "visibility timing records every resolved stage per slot");
        RequireContains(header, "resolvedStageMilliseconds",
            "visibility timing retains resolved values in their source slot");
        RequireContains(header,
            "std::array<TimerSlot, c_TimerLatency> m_TimerSlots{};",
            "visibility timing owns one snapshot for every latency slot");
        RequireContains(source, "if (timerSlot.resolvedStageMask !=",
            "visibility timing waits for every submitted query");
        RequireContains(source,
            "ScreenSpaceVisibilityTimings completedTimings = m_Timings;",
            "visibility timing assembles a complete snapshot before publish");
        RequireContains(source,
            "completedTimings.reconstructionMs = millisecondsOrZero(",
            "visibility timing zeros an omitted reconstruction stage");
        RequireContains(source, "m_Timings = completedTimings;",
            "visibility timing publishes all named values together");
        RequireContains(source, "timerSlot = {};",
            "visibility timing retires a completed slot before reuse");
        const size_t publishPosition = source.find(
            "m_Timings = completedTimings;");
        const size_t retirementPosition = source.find(
            "timerSlot = {};", publishPosition);
        const size_t writablePosition = source.find(
            "m_TimerFrameWritable = true;", retirementPosition);
        Require(publishPosition != std::string::npos &&
                retirementPosition != std::string::npos &&
                writablePosition != std::string::npos &&
                publishPosition < retirementPosition &&
                retirementPosition < writablePosition,
            "a latency slot becomes writable only after atomic publication "
            "and retirement");
        Require(source.find("m_Timings.firstTraceMs =") ==
                std::string::npos &&
                source.find("m_Timings.reconstructionMs =") ==
                    std::string::npos &&
                source.find("m_Timings.compositionMs =") ==
                    std::string::npos &&
                source.find("m_Timings.effectEnvelopeMs =") ==
                    std::string::npos,
            "resolved queries never mutate published stage fields piecemeal");
    }

    uint64_t Fnv1a64(const uint8_t* values, size_t count)
    {
        uint64_t hash = 14695981039346656037ull;
        for (size_t index = 0u; index < count; ++index)
        {
            hash ^= values[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    void ValidateNoiseAssetContract()
    {
        using namespace uvsr;

        const std::filesystem::path noiseDirectory =
            FindRepositoryRoot() / "assets/noise";
        const std::string manifest =
            ReadText(noiseDirectory / "manifest.json");
        RequireContains(manifest,
            "\"algorithm\": \"uvsr-spectral-stbn-v1\"",
            "noise manifest fixes the generator algorithm revision");
        RequireContains(manifest, "\"seed\": 1431720786",
            "noise manifest fixes the deterministic generator seed");
        RequireContains(manifest,
            "First-party clean-room spectral construction",
            "noise manifest records first-party provenance");
        Require(CountOccurrences(manifest, "\"file\":") ==
                ExpectedNoiseAssets.size(),
            "noise manifest enumerates exactly twelve assets");

        size_t binaryFileCount = 0u;
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator(noiseDirectory))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".bin")
                ++binaryFileCount;
        }
        Require(binaryFileCount == ExpectedNoiseAssets.size(),
            "the asset directory contains no undeclared noise binaries");

        for (const ExpectedNoiseAsset& expected : ExpectedNoiseAssets)
        {
            const uint32_t resolution =
                GetNoiseResolutionValue(expected.resolution);
            const uint32_t layers = GetNoiseLayerCount(expected.pattern);
            const uint64_t expectedBytes = uint64_t(resolution) *
                uint64_t(resolution) * uint64_t(layers);
            Require(std::string(GetNoiseAssetFileName(
                        expected.pattern, expected.resolution)) ==
                    expected.fileName,
                "settings and manifest use the same exact asset filename");

            const std::filesystem::path path =
                noiseDirectory / expected.fileName;
            const std::vector<uint8_t> bytes = ReadBinary(path);
            Require(uint64_t(bytes.size()) == expectedBytes,
                "noise asset dimensions match its raw R8 byte count");
            Require(Sha256(bytes) == expected.sha256,
                "noise asset SHA-256 matches the reviewed manifest");
            const auto range = std::minmax_element(
                bytes.begin(), bytes.end());
            Require(range.first != bytes.end() &&
                    *range.first == 0u && *range.second == 255u,
                "each noise asset spans the complete R8 code range");

            const std::string fileMarker =
                "\"file\": \"" + std::string(expected.fileName) + "\"";
            const size_t marker = manifest.find(fileMarker);
            Require(marker != std::string::npos,
                "noise manifest contains every expected filename");
            const size_t objectStart = manifest.rfind('{', marker);
            const size_t objectEnd = manifest.find('}', marker);
            Require(objectStart != std::string::npos &&
                    objectEnd != std::string::npos,
                "noise manifest asset entry is structurally complete");
            const std::string object = manifest.substr(
                objectStart, objectEnd - objectStart + 1u);
            RequireContains(object,
                "\"pattern\": \"" +
                    std::string(GetNoisePatternLabel(expected.pattern)) +
                    "\"",
                "manifest pattern label matches settings");
            RequireContains(object,
                "\"width\": " + std::to_string(resolution) + ",",
                "manifest width matches the selected resolution");
            RequireContains(object,
                "\"height\": " + std::to_string(resolution) + ",",
                "manifest height matches the selected resolution");
            RequireContains(object,
                "\"layers\": " + std::to_string(layers) + ",",
                "manifest layer count matches the selected pattern");
            RequireContains(object, "\"format\": \"R8_UNORM\"",
                "manifest fixes the runtime texture format");
            RequireContains(object,
                "\"bytes\": " + std::to_string(expectedBytes) + ",",
                "manifest records the exact raw payload size");
            RequireContains(object,
                "\"sha256\": \"" + std::string(expected.sha256) + "\"",
                "manifest records the reviewed payload hash");

            if (expected.pattern == NoisePattern::SpatiotemporalBlue)
            {
                const size_t sliceBytes =
                    size_t(resolution) * size_t(resolution);
                std::set<uint64_t> sliceHashes;
                for (uint32_t layer = 0u; layer < layers; ++layer)
                {
                    sliceHashes.insert(Fnv1a64(
                        bytes.data() + size_t(layer) * sliceBytes,
                        sliceBytes));
                }
                Require(sliceHashes.size() == layers,
                    "every ST Blue temporal slice is distinct");
            }
        }
    }

    double LowFrequencyPower(
        const std::vector<uint8_t>& values,
        uint32_t resolution,
        uint32_t layer)
    {
        const size_t sliceBytes =
            size_t(resolution) * size_t(resolution);
        const size_t offset = size_t(layer) * sliceBytes;
        Require(offset + sliceBytes <= values.size(),
            "spectral probe stays inside its selected noise slice");
        const double mean = std::accumulate(
            values.begin() + offset,
            values.begin() + offset + sliceBytes,
            0.0) / double(sliceBytes);

        double power = 0.0;
        uint32_t modeCount = 0u;
        for (int32_t ky = -2; ky <= 2; ++ky)
        for (int32_t kx = -2; kx <= 2; ++kx)
        {
            if (kx == 0 && ky == 0)
                continue;
            double real = 0.0;
            double imaginary = 0.0;
            for (uint32_t y = 0u; y < resolution; ++y)
            for (uint32_t x = 0u; x < resolution; ++x)
            {
                const double angle = 2.0 * Pi *
                    double(kx * int32_t(x) + ky * int32_t(y)) /
                    double(resolution);
                const double centered =
                    double(values[offset + size_t(y) * resolution + x]) -
                    mean;
                real += centered * std::cos(angle);
                imaginary -= centered * std::sin(angle);
            }
            power += real * real + imaginary * imaginary;
            ++modeCount;
        }
        return power / double(modeCount * resolution * resolution);
    }

    void ValidateNoiseSpectralAndTemporalSanity()
    {
        constexpr uint32_t resolution = 128u;
        const std::filesystem::path noiseDirectory =
            FindRepositoryRoot() / "assets/noise";
        const std::vector<uint8_t> white = ReadBinary(
            noiseDirectory / "spatial-white-128x128x1-r8.bin");
        const std::vector<uint8_t> blue = ReadBinary(
            noiseDirectory / "spatial-blue-128x128x1-r8.bin");
        const std::vector<uint8_t> spatiotemporal = ReadBinary(
            noiseDirectory / "spatiotemporal-blue-128x128x64-r8.bin");

        const double whiteLowPower =
            LowFrequencyPower(white, resolution, 0u);
        Require(LowFrequencyPower(blue, resolution, 0u) < whiteLowPower,
            "Spatial Blue suppresses low spatial frequencies versus white");
        constexpr std::array<uint32_t, 6> probeLayers = {
            0u, 1u, 7u, 19u, 37u, 63u
        };
        double stLowPower = 0.0;
        for (uint32_t layer : probeLayers)
            stLowPower += LowFrequencyPower(
                spatiotemporal, resolution, layer);
        stLowPower /= double(probeLayers.size());
        Require(stLowPower < whiteLowPower,
            "ST Blue slices suppress low spatial frequencies versus white");

        constexpr uint32_t layerCount = 64u;
        const size_t sliceBytes = size_t(resolution) * resolution;
        constexpr std::array<std::array<uint32_t, 2>, 8> probes = {{
            {{ 0u, 0u }}, {{ 17u, 29u }}, {{ 63u, 71u }},
            {{ 127u, 127u }}, {{ 42u, 99u }}, {{ 11u, 53u }},
            {{ 91u, 7u }}, {{ 103u, 117u }}
        }};
        double absoluteSerialCorrelation = 0.0;
        for (const auto& probe : probes)
        {
            std::array<double, layerCount> sequence{};
            std::set<uint8_t> distinct;
            double mean = 0.0;
            const size_t pixel = size_t(probe[1]) * resolution + probe[0];
            for (uint32_t layer = 0u; layer < layerCount; ++layer)
            {
                const uint8_t value =
                    spatiotemporal[size_t(layer) * sliceBytes + pixel];
                sequence[layer] = double(value);
                distinct.insert(value);
                mean += value;
            }
            mean /= double(layerCount);
            Require(distinct.size() == layerCount,
                "each fixed ST Blue pixel traverses 64 distinct R8 values");

            double covariance = 0.0;
            double variance = 0.0;
            for (uint32_t layer = 0u; layer < layerCount; ++layer)
            {
                const double centered = sequence[layer] - mean;
                covariance += centered *
                    (sequence[(layer + 1u) & 63u] - mean);
                variance += centered * centered;
            }
            Require(variance > 0.0,
                "each ST Blue temporal probe has nonzero variance");
            absoluteSerialCorrelation += std::abs(covariance / variance);
        }
        absoluteSerialCorrelation /= double(probes.size());
        Require(absoluteSerialCorrelation < 0.5,
            "ST Blue temporal probes avoid repeated or alternating slices");
    }

    bool IsIndependentContributionDiscovery(
        float centerSeed,
        float neighboringSeed,
        float independentImportance)
    {
        return centerSeed > 0.f ||
            neighboringSeed * 0.75f <= independentImportance + 1e-6f;
    }

    struct VisibilityTraceFixtureSample
    {
        uint32_t intervalMask = 0u;
        uint32_t samplePixel = 0u;
        uint32_t side = 0u;
        float sourceRadiance = 0.f;
    };

    struct VisibilityTraceFixtureResult
    {
        uint32_t mask = 0u;
        uint32_t newlyClaimedBitCount = 0u;
        float rawAmbient = 1.f;
        float rawIndirect = 0.f;
        std::array<int32_t, 32> sourceOwner{};
    };

    uint32_t CountBits(uint32_t value)
    {
        uint32_t count = 0u;
        while (value != 0u)
        {
            value &= value - 1u;
            ++count;
        }
        return count;
    }

    VisibilityTraceFixtureResult RunVisibilityTraceFixture(
        const std::vector<VisibilityTraceFixtureSample>& samples,
        bool rejectDuplicatePixels,
        bool exitOnFullMask)
    {
        VisibilityTraceFixtureResult result;
        result.sourceOwner.fill(-1);
        std::array<uint32_t, 2> previousPixel{};
        std::array<bool, 2> hasPrevious{};
        for (size_t sampleIndex = 0u;
            sampleIndex < samples.size();
            ++sampleIndex)
        {
            const VisibilityTraceFixtureSample& sample = samples[sampleIndex];
            Require(sample.side < previousPixel.size(),
                "trace fixture uses a valid radial side");
            if (rejectDuplicatePixels && hasPrevious[sample.side] &&
                previousPixel[sample.side] == sample.samplePixel)
            {
                continue;
            }
            hasPrevious[sample.side] = true;
            previousPixel[sample.side] = sample.samplePixel;

            const uint32_t newlyVisible = sample.intervalMask & ~result.mask;
            result.mask |= sample.intervalMask;
            const uint32_t claimedCount = CountBits(newlyVisible);
            result.newlyClaimedBitCount += claimedCount;
            result.rawIndirect += sample.sourceRadiance *
                (float(claimedCount) / 32.f);
            for (uint32_t bit = 0u; bit < 32u; ++bit)
            {
                if ((newlyVisible & (uint32_t{ 1 } << bit)) != 0u)
                    result.sourceOwner[bit] = int32_t(sampleIndex);
            }
            if (exitOnFullMask && result.mask == ~uint32_t{ 0 })
                break;
        }
        result.rawAmbient = 1.f -
            float(CountBits(result.mask)) / 32.f;
        return result;
    }

    void RequireEquivalentVisibilityResult(
        const VisibilityTraceFixtureResult& left,
        const VisibilityTraceFixtureResult& right,
        const std::string& control)
    {
        Require(left.mask == right.mask,
            control + " preserves the final mask");
        Require(left.newlyClaimedBitCount == right.newlyClaimedBitCount,
            control + " preserves newly claimed bit counts");
        Require(std::abs(left.rawAmbient - right.rawAmbient) < 1e-7f,
            control + " preserves raw AO");
        Require(std::abs(left.rawIndirect - right.rawIndirect) < 1e-7f,
            control + " preserves raw GI");
        Require(left.sourceOwner == right.sourceOwner,
            control + " preserves near-to-far source ownership");
    }

    void ValidateExactTraceControls()
    {
        const std::vector<VisibilityTraceFixtureSample> duplicateFixture = {
            { 0x000000ffu, 17u, 0u, 0.25f },
            // Same side, pixel, interval, and source as the preceding tap. The
            // second OR is idempotent and owns no new sector when retained.
            { 0x000000ffu, 17u, 0u, 0.25f },
            { 0x0000ff00u, 31u, 0u, 0.5f },
            { 0xffff0000u, 42u, 1u, 0.75f }
        };
        RequireEquivalentVisibilityResult(
            RunVisibilityTraceFixture(duplicateFixture, true, false),
            RunVisibilityTraceFixture(duplicateFixture, false, false),
            "duplicate-pixel rejection");

        const std::vector<VisibilityTraceFixtureSample> fullMaskFixture = {
            { 0x0000ffffu, 11u, 0u, 0.25f },
            { 0xffff0000u, 12u, 1u, 0.75f },
            // This farther source cannot own a sector after the mask is full.
            { 0xffffffffu, 13u, 1u, 100.f }
        };
        RequireEquivalentVisibilityResult(
            RunVisibilityTraceFixture(fullMaskFixture, true, true),
            RunVisibilityTraceFixture(fullMaskFixture, true, false),
            "full-mask early exit");
    }

    double HorizonAcos(double horizonCosine)
    {
        return std::acos(std::clamp(horizonCosine, -1.0, 1.0));
    }

    double HorizonArc(double horizonAngle, double normalAngle)
    {
        return 0.25 * (
            std::cos(normalAngle) +
            2.0 * horizonAngle * std::sin(normalAngle) -
            std::cos(2.0 * horizonAngle - normalAngle));
    }

    double NumericalHorizonArc(double horizonAngle, double normalAngle)
    {
        constexpr uint32_t intervalCount = 32768u;
        const double interval = horizonAngle / double(intervalCount);
        double integral = 0.0;
        for (uint32_t index = 0u; index < intervalCount; ++index)
        {
            const double angle = (double(index) + 0.5) * interval;
            integral += std::sin(angle) *
                std::cos(angle - normalAngle) * interval;
        }
        return integral;
    }

    struct HorizonIntegral
    {
        double negativeAngle = 0.0;
        double positiveAngle = 0.0;
        double rawVisibility = 0.0;
        double boundedVisibility = 0.0;
    };

    HorizonIntegral EvaluateHorizonIntegral(
        double negativeHorizonCosine,
        double positiveHorizonCosine,
        double signedNormalAngle)
    {
        constexpr double halfPi = Pi * 0.5;
        HorizonIntegral result;
        result.negativeAngle = -HorizonAcos(negativeHorizonCosine);
        result.positiveAngle = HorizonAcos(positiveHorizonCosine);

        // Intersect the view-facing horizon interval with the cosine lobe of
        // the projected normal. This happens after acos so a negative horizon
        // cosine is not silently reinterpreted as an open, zero-cosine edge.
        result.negativeAngle = signedNormalAngle + std::max(
            result.negativeAngle - signedNormalAngle, -halfPi);
        result.positiveAngle = signedNormalAngle + std::min(
            result.positiveAngle - signedNormalAngle, halfPi);
        result.rawVisibility =
            HorizonArc(result.negativeAngle, signedNormalAngle) +
            HorizonArc(result.positiveAngle, signedNormalAngle);
        result.boundedVisibility = std::clamp(
            result.rawVisibility, 0.0, 1.0);
        return result;
    }

    void ValidateHorizonIntegralReference()
    {
        constexpr double halfPi = Pi * 0.5;
        for (double horizonCosine : { -1.0, -0.75, -0.25, -0.001 })
        {
            const double horizonAngle = HorizonAcos(horizonCosine);
            Require(horizonAngle > halfPi && horizonAngle <= Pi,
                "negative horizon cosines survive full-domain acos");
            Require(Near(std::cos(horizonAngle), horizonCosine),
                "full-domain acos preserves a negative horizon cosine");
        }
        Require(Near(HorizonAcos(-1.25), Pi) &&
                Near(HorizonAcos(1.25), 0.0),
            "horizon acos clamps numerical drift to the full unit domain");

        constexpr std::array<double, 5> signedNormalAngles = {
            -Pi / 3.0, -Pi / 6.0, 0.0, Pi / 6.0, Pi / 3.0
        };
        for (double normalAngle : signedNormalAngles)
        {
            const HorizonIntegral open = EvaluateHorizonIntegral(
                0.0, 0.0, normalAngle);
            const double expectedNegativeAngle = normalAngle > 0.0
                ? normalAngle - halfPi
                : -halfPi;
            Require(Near(open.negativeAngle, expectedNegativeAngle),
                "an open lower horizon is clipped only by the normal lobe");
            Require(Near(
                    HorizonArc(open.negativeAngle, normalAngle),
                    NumericalHorizonArc(open.negativeAngle, normalAngle),
                    1e-8),
                "the open lower-horizon analytic integral matches quadrature");
            Require(std::isfinite(open.rawVisibility) &&
                    open.rawVisibility >= 0.0 &&
                    open.rawVisibility <= 1.0 + 1e-12,
                "representative open-horizon integrals are finite and bounded");
            const HorizonIntegral mirrored = EvaluateHorizonIntegral(
                0.0, 0.0, -normalAngle);
            Require(Near(open.rawVisibility, mirrored.rawVisibility),
                "open-horizon visibility is symmetric about the view vector");
        }

        constexpr std::array<double, 7> horizonCosines = {
            -1.0, -0.75, -0.25, 0.0, 0.25, 0.75, 1.0
        };
        for (uint32_t normalIndex = 0u; normalIndex <= 32u; ++normalIndex)
        {
            const double normalAngle = -halfPi +
                double(normalIndex) * Pi / 32.0;
            for (double negativeCosine : horizonCosines)
            for (double positiveCosine : horizonCosines)
            {
                const HorizonIntegral value = EvaluateHorizonIntegral(
                    negativeCosine, positiveCosine, normalAngle);
                const HorizonIntegral mirrored = EvaluateHorizonIntegral(
                    positiveCosine, negativeCosine, -normalAngle);
                Require(std::isfinite(value.rawVisibility) &&
                        value.rawVisibility >= -1e-12 &&
                        value.rawVisibility <= halfPi + 1e-12,
                    "horizon integration remains finite within its analytic bound");
                Require(value.boundedVisibility >= 0.0 &&
                        value.boundedVisibility <= 1.0,
                    "the resolved visibility remains in the AO output domain");
                Require(Near(
                        value.rawVisibility, mirrored.rawVisibility, 1e-8),
                    "swapping horizon sides and normal sign preserves visibility");
            }
        }
    }
}

int main()
{
    ValidateAtomicVisibilityTimingReference();
    ValidateAtomicVisibilityTimingSourceContract();
    ValidateNoiseSettingsContract();
    ValidateCenteredSamplingContract();
    ValidateNoiseAssetContract();
    ValidateNoiseSpectralAndTemporalSanity();
    ValidateExactTraceControls();
    ValidateHorizonIntegralReference();

    Require(IsIndependentContributionDiscovery(0.f, 0.f, 0.f),
        "a baseline discovery may create an initial contribution seed");
    Require(!IsIndependentContributionDiscovery(0.f, 1.f, 0.f),
        "neighbor-driven work cannot become a second-hop seed");
    Require(IsIndependentContributionDiscovery(1.f, 1.f, 0.f),
        "a reprojected center seed can persist without spatial dilation");
    Require(IsIndependentContributionDiscovery(0.f, 1.f, 1.f),
        "independently difficult pixels remain eligible contribution seeds");

    std::cout << "Visibility sampling validation passed\n";
    return EXIT_SUCCESS;
}
