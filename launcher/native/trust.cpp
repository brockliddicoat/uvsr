#include "core.h"
#include <bcrypt.h>
#include <array>
#include <cstring>

namespace uvsr::launcher
{
    void VerifyP256Signature(std::span<const unsigned char> payload,
        std::span<const unsigned char> signature, std::string_view keyText)
    {
        const auto spki = DecodeBase64(keyText);
        // exact DER for id-ecPublicKey + prime256v1 + uncompressed point.
        constexpr unsigned char prefix[] = {
            0x30,0x59,0x30,0x13,0x06,0x07,0x2a,0x86,0x48,0xce,0x3d,0x02,0x01,
            0x06,0x08,0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07,0x03,0x42,0x00,0x04};
        Require(spki.size() == sizeof(prefix) + 64 && !memcmp(spki.data(), prefix, sizeof(prefix)),
            "The update signing key is not the required P-256 key.");
        Require(signature.size() == 64, "The update signature is not a 64-byte IEEE P1363 value.");
        struct Key
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_KEY_HANDLE value = nullptr;
            ~Key() { if (value) BCryptDestroyKey(value); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
        } key;
        Require(BCryptOpenAlgorithmProvider(&key.algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) >= 0, "Windows P-256 verification is unavailable.");
        struct Blob { BCRYPT_ECCKEY_BLOB header; unsigned char point[64]; } blob{};
        blob.header = {BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32};
        memcpy(blob.point, spki.data() + sizeof(prefix), 64);
        Require(BCryptImportKeyPair(key.algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB,
            &key.value, reinterpret_cast<PUCHAR>(&blob), sizeof(blob), 0) >= 0, "The signing key could not be imported.");
        auto digest = HashBytes(payload);
        Require(BCryptVerifySignature(key.value, nullptr, digest.data(), ULONG(digest.size()),
            const_cast<PUCHAR>(signature.data()), ULONG(signature.size()), 0) >= 0,
            "The update feed signature is invalid. No update was trusted.");
    }
    namespace
    {
        void ValidateSequence(int64_t sequence)
        { Require(sequence >= 1 && sequence <= MaximumSequence, "The release sequence is outside its safe range."); }
    }
    Feed VerifyFeed(std::string_view text, Component component, std::string_view key, std::string_view keyId)
    {
        Require(!text.empty() && text.size() <= 16384, "The update feed envelope is empty or too large.");
        const auto envelope = ParseJson(text);
        RequireExactObject(envelope, {"schemaVersion", "keyId", "payloadBase64", "signatureBase64"}, "feed envelope");
        const int schema = component == Component::Launcher ? 2 : 1;
        Require(Number(envelope, "schemaVersion") == schema && Text(envelope, "keyId") == keyId, "The update feed schema or key identity is not trusted.");
        auto payload = DecodeBase64(Text(envelope, "payloadBase64"));
        auto signature = DecodeBase64(Text(envelope, "signatureBase64"));
        Require(!payload.empty() && payload.size() <= 8192, "The update feed payload is empty or too large.");
        VerifyP256Signature(payload, signature, key);
        auto object = ParseJson(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
        if (component == Component::Launcher)
            RequireExactObject(object, {"schemaVersion", "productId", "channel", "releaseSequence", "version", "sourceCommit", "artifact"}, "launcher feed");
        else
            RequireExactObject(object, {"schemaVersion", "productId", "channel", "releaseSequence", "sourceCommit", "settingsHash", "engineVersion", "artifact"}, "renderer feed");
        Require(Number(object, "schemaVersion") == schema && Text(object, "productId") == ProductId && Text(object, "channel") == "stable", "The update feed identity is not canonical.");
        Feed feed;
        feed.component = component;
        feed.sequence = Number(object, "releaseSequence"); ValidateSequence(feed.sequence);
        feed.commit = Text(object, "sourceCommit");
        Require(IsLowerHex(feed.commit, 40), "The feed source commit is invalid.");
        feed.version = Text(object, component == Component::Launcher ? "version" : "engineVersion");
        Require(IsCanonicalDottedVersion(feed.version, component == Component::Launcher ? 3 : 4,
            component == Component::Launcher ? INT32_MAX : 65535), "The feed version is invalid.");
        if (component == Component::Renderer)
        {
            feed.settingsHash = Text(object, "settingsHash");
            Require(IsLowerHex(feed.settingsHash, 32), "The feed settings hash is invalid.");
        }
        const auto& artifact = Member(object, "artifact");
        RequireExactObject(artifact, {"name", "size", "sha256"}, "feed artifact");
        if (component == Component::Launcher)
        {
            const auto orderedArtifact = JObject({{"name", Member(artifact, "name")}, {"size", Member(artifact, "size")}, {"sha256", Member(artifact, "sha256")}});
            const auto orderedPayload = JObject({{"schemaVersion", Member(object, "schemaVersion")}, {"productId", Member(object, "productId")},
                {"channel", Member(object, "channel")}, {"releaseSequence", Member(object, "releaseSequence")}, {"version", Member(object, "version")},
                {"sourceCommit", Member(object, "sourceCommit")}, {"artifact", orderedArtifact}});
            const auto orderedEnvelope = JObject({{"schemaVersion", Member(envelope, "schemaVersion")}, {"keyId", Member(envelope, "keyId")},
                {"payloadBase64", Member(envelope, "payloadBase64")}, {"signatureBase64", Member(envelope, "signatureBase64")}});
            Require(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()) == Serialize(orderedPayload) + "\n" &&
                text == Serialize(orderedEnvelope) + "\n", "The launcher feed bytes are not canonical.");
        }
        const auto size = Number(artifact, "size");
        Require(size > 0 && uint64_t(size) <= (component == Component::Launcher ? MaximumLauncherBytes : MaximumArchiveBytes), "The feed artifact size is outside its safe range.");
        Require(Text(artifact, "name") == (component == Component::Launcher ? LauncherName : ArchiveName), "The feed artifact name is not canonical.");
        feed.size = uint64_t(size); feed.hash = Text(artifact, "sha256");
        Require(IsLowerHex(feed.hash, 64), "The feed artifact SHA-256 is invalid.");
        return feed;
    }
    std::string ArtifactUrl(const Feed& feed)
    {
        return "https://github.com/brockliddicoat/uvsr/releases/download/" +
            (feed.component == Component::Launcher ? "uvsr-launcher-v" + feed.version + "/" + LauncherName :
            "uvsr-engine-r" + std::to_string(feed.sequence) + "/" + ArchiveName);
    }
    void ValidateState(const Json& state, std::string_view installation, Component component)
    {
        if (component == Component::Launcher)
        {
            RequireExactObject(state, {"schemaVersion", "productId", "installationId", "releaseSequence", "version", "executableSha256", "desktopShortcut", "installedUtc"}, "launcher state");
            Require(Text(state, "productId") == ProductId && IsCanonicalDottedVersion(Text(state, "version"), 3, INT32_MAX), "The installed launcher identity is invalid.");
        }
        else
        {
            RequireExactObject(state, {"schemaVersion", "installationId", "activeVersionId", "releaseSequence", "commit", "settingsHash", "engineVersion", "artifactSha256", "executableSha256", "desktopShortcut", "installedUtc"}, "renderer state");
            Require(IsVersionId(Text(state, "activeVersionId")) && IsLowerHex(Text(state, "commit"), 40) &&
                IsLowerHex(Text(state, "settingsHash"), 32) && IsLowerHex(Text(state, "artifactSha256"), 64) &&
                IsCanonicalDottedVersion(Text(state, "engineVersion"), 4, 65535), "The installed renderer identity is invalid.");
            Require(Text(state, "activeVersionId").starts_with(Text(state, "commit") + "-"), "The renderer directory does not bind its source commit.");
        }
        ValidateSequence(Number(state, "releaseSequence"));
        Require(Number(state, "schemaVersion") == 1 && IsGuid(installation) && Text(state, "installationId") == installation && IsLowerHex(Text(state, "executableSha256"), 64), "The installed ownership record is invalid.");
        (void)Flag(state, "desktopShortcut");
        const auto& time = Text(state, "installedUtc");
        Require(time.size() >= 20 && time.size() <= 40 && time[4] == '-' && time[7] == '-' && time[10] == 'T', "The installed date is invalid.");
    }
    UpdateState Classify(const std::optional<Json>& state, bool healthy, const Feed& feed)
    {
        if (!state) return healthy ? UpdateState::NotInstalled : UpdateState::RepairNeeded;
        auto sequence = Number(*state, "releaseSequence");
        if (feed.sequence < sequence) return UpdateState::Current;
        if (feed.sequence > sequence) return UpdateState::UpdateAvailable;
        bool equal = feed.component == Component::Launcher ?
            Text(*state, "version") == feed.version && HashEqual(Text(*state, "executableSha256"), feed.hash) :
            Text(*state, "commit") == feed.commit && Text(*state, "settingsHash") == feed.settingsHash &&
            Text(*state, "engineVersion") == feed.version && HashEqual(Text(*state, "artifactSha256"), feed.hash);
        Require(equal, "The update feed reused a release sequence with different files or settings.");
        return healthy ? UpdateState::Current : UpdateState::RepairNeeded;
    }
    void ValidateLauncherMetadata(const fs::path& path, std::string_view version, std::optional<std::string_view> commit)
    {
        ValidatePe(path);
        const auto productVersion = PeString(path, L"ProductVersion");
        Require(PeString(path, L"ProductName") == "UVSR Launcher" && PeString(path, L"FileVersion") == std::string(version) + ".0" &&
            (commit ? productVersion == std::string(version) + "+" + std::string(*commit) :
            productVersion == version || productVersion.starts_with(std::string(version) + "+")),
            "The launcher file has unexpected product metadata.");
    }
}
