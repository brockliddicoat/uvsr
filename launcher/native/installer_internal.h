#pragma once
#include "installer.h"
#include <algorithm>

namespace uvsr::launcher
{
    inline std::optional<Json> Optional(const Json& value)
    { return value.kind == Json::Kind::Null ? std::nullopt : std::optional<Json>(value); }
    inline Json Nullable(const std::optional<Json>& value) { return value.value_or(Json{}); }
    inline std::optional<Json> ReadState(const fs::path& path, std::string_view installation, Component component)
    {
        RejectReparseChain(path);
        if (!fs::exists(path)) return {};
        auto state = ReadRecord(path); ValidateState(state, installation, component); return state;
    }
    inline void RestoreState(const fs::path& path, const std::optional<Json>& state)
    { if (state) WriteRecord(path, *state); else { RejectReparseChain(path); fs::remove(path); } }
    inline bool Unverifiable(const std::exception& error)
    {
        if (const auto* access = dynamic_cast<const AccessError*>(&error))
            return access->code != ERROR_FILE_NOT_FOUND && access->code != ERROR_PATH_NOT_FOUND;
        return dynamic_cast<const fs::filesystem_error*>(&error) != nullptr;
    }
    inline std::string CompactGuid(std::string value) { std::erase(value, '-'); return value; }
    inline Json LauncherStateFromMarker(const Json& marker, bool desktop)
    {
        RequireExactObject(marker, {"schemaVersion", "productId", "installationId", "releaseSequence", "version", "executableSha256", "executableSize", "installedUtc"}, "launcher package marker");
        return JObject({{"schemaVersion", Member(marker, "schemaVersion")}, {"productId", Member(marker, "productId")},
            {"installationId", Member(marker, "installationId")}, {"releaseSequence", Member(marker, "releaseSequence")},
            {"version", Member(marker, "version")}, {"executableSha256", Member(marker, "executableSha256")},
            {"desktopShortcut", JBool(desktop)}, {"installedUtc", Member(marker, "installedUtc")}});
    }
    inline void SameLauncherIdentity(const Json& a, const Json& b)
    {
        Require(Number(a, "releaseSequence") == Number(b, "releaseSequence") && Text(a, "version") == Text(b, "version") &&
            HashEqual(Text(a, "executableSha256"), Text(b, "executableSha256")), "Conflicting launcher files reuse the same release sequence.");
    }
    inline void ValidateLauncherJournal(const Json& journal, std::string_view installation)
    {
        RequireExactObject(journal, {"schemaVersion", "productId", "installationId", "transactionId", "phase", "previousState", "candidateState", "continueUvsrUpdate", "startedUtc"}, "launcher activation journal");
        Require(Number(journal, "schemaVersion") == 1 && Text(journal, "productId") == ProductId && Text(journal, "installationId") == installation &&
            IsGuid(Text(journal, "transactionId")), "The launcher activation journal has an invalid owner.");
        const auto& phase = Text(journal, "phase");
        Require(phase == "prepared" || phase == "state-activated" || phase == "shell-committed" || phase == "awaiting-continuation" || phase == "continuation-complete", "The launcher activation phase is invalid.");
        ValidateState(Member(journal, "candidateState"), installation, Component::Launcher);
        if (auto previous = Optional(Member(journal, "previousState"))) ValidateState(*previous, installation, Component::Launcher);
        (void)Flag(journal, "continueUvsrUpdate"); (void)Text(journal, "startedUtc");
    }
    inline void RemoveEmptyParents(fs::path directory, const fs::path& root)
    {
        while (IsDescendant(directory, root) && fs::exists(directory))
        {
            RejectReparseChain(directory); if (!fs::is_directory(directory) || !fs::is_empty(directory)) break;
            fs::remove(directory); directory = directory.parent_path();
        }
    }
    void DeleteValidatedPackage(const Paths& paths, const fs::path& root, std::span<const PackageFile> files, std::string_view manifestName);
}
