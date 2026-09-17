#include "settings_snapshot_decoder.h"
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uvsr;
void Require(bool accepted, const char* reason)
{
    if (!accepted) { fprintf(stderr, "%s\n", reason); exit(1); }
}
namespace
{
    bool EmitCopy(json::OutputWriter& output, const void* context) noexcept
    {
        const auto text = *static_cast<const std::string_view*>(context);
        return output.Raw({text.data(), text.size()});
    }
    json::EncodedText Text(std::string_view value)
    {
        json::EncodedText output(EmitCopy, &value);
        Require(output.IsValid(), "cannot prepare text control");
        return output;
    }
    void ErrorOwnership()
    {
        SettingsSnapshotError source{SettingsSnapshotErrorCode::Path, 91, 92, "static reason", {}};
        SettingsSnapshotError output;
        SettingsSnapshotError failure{SettingsSnapshotErrorCode::Format, 0, 0, "old failure", {}};
        json::FailAllocationAfter(0);
        const bool staticClone = source.CloneTo(output, failure);
        json::ClearAllocationFailure();
        Require(staticClone && failure.code == SettingsSnapshotErrorCode::None &&
            output.code == source.code && output.nativeCode == 91 && output.cleanupCode == 92 &&
            output.message == source.message && !output.detail.IsValid(), "static error clone allocated or lost fields");
        source.detail = Text({"a\0b", 3});
        const auto sourceView = source.MessageView();
        output.detail = Text("previous output");
        const auto outputView = output.MessageView();
        json::FailAllocationAfter(0);
        const bool cloned = source.CloneTo(output, failure);
        json::ClearAllocationFailure();
        Require(!cloned && failure.code == SettingsSnapshotErrorCode::OutOfMemory &&
            source.MessageView().data() == sourceView.data() && source.MessageView() == sourceView &&
            output.MessageView().data() == outputView.data() && output.MessageView() == outputView,
            "failed error clone changed an owner or its views");
        Require(source.CloneTo(output, failure) && output.MessageView() == std::string_view("a\0b", 3) &&
            output.MessageView().data() != source.MessageView().data() && output.code == source.code &&
            output.nativeCode == 91 && output.cleanupCode == 92 && failure.code == SettingsSnapshotErrorCode::None,
            "dynamic error clone lost bytes or numeric fields");
        failure = {SettingsSnapshotErrorCode::Format, 0, 0, "stale", {}};
        const auto selfView = source.MessageView();
        json::FailAllocationAfter(0);
        const bool selfClone = source.CloneTo(source, failure);
        json::ClearAllocationFailure();
        Require(selfClone && source.MessageView().data() == selfView.data() &&
            failure.code == SettingsSnapshotErrorCode::None, "self clone changed the error or kept stale failure");
        const auto oldOutput = output.MessageView();
        Require(!source.CloneTo(output, output) && !source.CloneTo(output, source) &&
            source.MessageView().data() == selfView.data() && output.MessageView().data() == oldOutput.data(),
            "an aliased failure argument changed an error owner");
        source = ComposeSettingsSnapshotError({"prefix:", source.MessageView(), ":suffix"},
            source.code, source.nativeCode, source.cleanupCode);
        Require(source.MessageView() == std::string_view("prefix:a\0b:suffix", 17) &&
            source.code == SettingsSnapshotErrorCode::Path && source.nativeCode == 91 && source.cleanupCode == 92,
            "error composition lost an aliased message or its fields");
        json::FailAllocationAfter(0);
        auto exhausted = ComposeSettingsSnapshotError({source.MessageView()}, source.code, 91, 92);
        json::ClearAllocationFailure();
        Require(exhausted.code == SettingsSnapshotErrorCode::OutOfMemory && exhausted.nativeCode == 91 &&
            exhausted.cleanupCode == 92 && !exhausted.MessageView().empty() &&
            source.MessageView() == std::string_view("prefix:a\0b:suffix", 17),
            "diagnostic exhaustion lost numeric provenance or changed its source");
    }
    void DecoderFailures()
    {
        SettingsSnapshotError error;
        DecodedSettings owner;
        Require(owner.Insert("old", "value", error), "cannot prepare decoder failure owner");
        const auto* originalEntries = owner.Entries();
        for (size_t failure = 0; failure != 4; ++failure)
        {
            FailSettingsSnapshotAllocationAfter(failure);
            const bool accepted = ParseSettingsSnapshot("a=1\nb=2\nc=3\n", owner, error);
            ClearSettingsSnapshotAllocationFailure();
            Require(!accepted && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
                owner.Entries() == originalEntries && owner.Count() == 1 && owner.Find("old")->value == "value",
                "failed parse published partial entries");
        }
        for (size_t failure = 0; failure != 3; ++failure)
        {
            json::FailAllocationAfter(failure);
            const bool accepted = ParseSettingsSnapshot("a=1\nb=2\nc=3\n", owner, error);
            json::ClearAllocationFailure();
            Require(!accepted && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
                owner.Entries() == originalEntries && owner.Count() == 1,
                "failed value decoding replaced its destination");
        }
        Require(ParseSettingsSnapshot("a=1\nb=2\nc=3\n", owner, error) && owner.Count() == 3,
            "parse retry failed");
        json::EncodedText output = Text("previous text");
        const char* originalText = output.Data();
        for (int operation = 0; operation != 3; ++operation)
        {
            json::FailAllocationAfter(0);
            const bool accepted = operation == 0 ? FormatCanonicalSettingsSnapshot(owner, output, error)
                : operation == 1 ? FormatDecodedSettingsJson(owner, output, error)
                : UnescapeSettingsSnapshotValue("new\\ntext", output, error);
            json::ClearAllocationFailure();
            Require(!accepted && error.code == SettingsSnapshotErrorCode::OutOfMemory && output.Data() == originalText &&
                std::string_view(output.Data(), output.Size()) == "previous text", "failed text output lost its prior owner");
        }
        Require(!ParseSettingsSnapshot({nullptr, 1}, owner, error) && owner.Count() == 3 &&
            !UnescapeSettingsSnapshotValue({reinterpret_cast<const char*>(UINTPTR_MAX - 1), 5}, output, error),
            "invalid text range was accepted");
        error.detail = Text("diagnostic text");
        const std::string_view diagnostic(error.Message(), error.detail.Size());
        Require(owner.Set(diagnostic, diagnostic, error) && owner.Find("diagnostic text") &&
            owner.Find("diagnostic text")->value == "diagnostic text", "diagnostic input alias lost its owner");
        error.detail = Text("diagnostic\\nvalue");
        Require(UnescapeSettingsSnapshotValue({error.Message(), error.detail.Size()}, output, error) &&
            std::string_view(output.Data(), output.Size()) == "diagnostic\nvalue", "unescape lost its diagnostic input");
        error.detail = Text("diagnostic=value\n");
        Require(ParseSettingsSnapshot({error.Message(), error.detail.Size()}, owner, error) && owner.Count() == 1 &&
            owner.Find("diagnostic")->value == "value", "parse lost its diagnostic input");
        Require(owner.Set("payload", "replacement=kept\n", error), "cannot prepare owner alias");
        Require(ParseSettingsSnapshot(owner.Find("payload")->value, owner, error) && owner.Count() == 1 &&
            owner.Find("replacement")->value == "kept", "parse lost its destination-derived input");
        const std::string_view names[] = {{"a\0z", 3}, "aa", "a", "", "aaa", "b"};
        owner.Clear();
        for (const auto name : names) Require(owner.Set(name, name, error), "cannot prepare prefix keys");
        const std::string_view expected[] = {"", "a", {"a\0z", 3}, "aa", "aaa", "b"};
        Require(owner.Count() == 6, "prefix keys lost a distinct byte string");
        for (size_t index = 0; index < owner.Count(); ++index)
            Require(owner.Entries()[index].name == expected[index], "unequal-length prefix order changed");
    }
    void CatalogOwners()
    {
        SettingsSnapshotError error;
        SettingsSnapshotCatalogPaths paths;
        for (unsigned count = 0; count != 65; ++count)
        {
            paths.Clear();
            for (unsigned index = count; index != 0; --index)
            {
                wchar_t name[16];
                Require(swprintf_s(name, L"%04u", index - 1) > 0 && paths.Append(name, error), "cannot prepare sorting paths");
            }
            paths.Sort();
            for (unsigned index = 0; index < count; ++index)
            {
                wchar_t expected[16];
                Require(swprintf_s(expected, L"%04u", index) > 0 && wcscmp(paths.Path(index), expected) == 0,
                    "catalog path sort changed order");
            }
        }
        paths.Clear();
        for (int index = 0; index != 8; ++index) Require(paths.Append(L"aliased path", error), "cannot prepare path growth");
        const wchar_t* original = paths.Path(0);
        for (size_t failure = 0; failure != 2; ++failure)
        {
            FailSettingsSnapshotAllocationAfter(failure);
            const bool accepted = paths.Append(original, error);
            ClearSettingsSnapshotAllocationFailure();
            Require(!accepted && paths.Count() == 8 && paths.Path(0) == original &&
                wcscmp(paths.Path(0), L"aliased path") == 0, "failed path growth changed the owner");
        }
        Require(paths.Append(original, error) && paths.Count() == 9, "path alias retry failed");
        SettingsSnapshotCatalogPaths moved(static_cast<SettingsSnapshotCatalogPaths&&>(paths));
        Require(!paths.Count() && moved.Count() == 9, "path move construction failed");
        paths = static_cast<SettingsSnapshotCatalogPaths&&>(moved);
        paths = static_cast<SettingsSnapshotCatalogPaths&&>(paths);
        Require(paths.Count() == 9 && !moved.Count(), "path move assignment failed");
        Require(!paths.Append(nullptr, error) && paths.Count() == 9, "null path changed the owner");

        SettingsSnapshotMatches matches;
        for (int index = 0; index != 4; ++index)
        {
            auto text = Text(index ? "payload" : "");
            Require(matches.Append(static_cast<json::EncodedText&&>(text), error) && !text.IsValid(),
                "matching payload ownership did not transfer");
        }
        auto text = Text("last payload");
        const auto originalPayload = matches.Text(1);
        FailSettingsSnapshotAllocationAfter(0);
        const bool appended = matches.Append(static_cast<json::EncodedText&&>(text), error);
        ClearSettingsSnapshotAllocationFailure();
        Require(!appended && text.IsValid() && matches.Count() == 4 &&
            matches.Text(1).data() == originalPayload.data() && matches.Text(0).empty(),
            "failed payload growth changed its source or destination");
        Require(matches.Append(static_cast<json::EncodedText&&>(text), error) && !text.IsValid() &&
            matches.Text(4) == "last payload", "matching payload retry failed");
        SettingsSnapshotMatches movedMatches(static_cast<SettingsSnapshotMatches&&>(matches));
        Require(!matches.Count() && movedMatches.Count() == 5, "matching payload move failed");
        matches = static_cast<SettingsSnapshotMatches&&>(movedMatches);
        matches = static_cast<SettingsSnapshotMatches&&>(matches);
        Require(matches.Count() == 5 && !movedMatches.Count(), "matching payload assignment failed");
        json::EncodedText invalid;
        Require(!matches.Append(static_cast<json::EncodedText&&>(invalid), error) && matches.Count() == 5,
            "invalid matching payload changed the owner");
        error.detail = Text("diagnostic payload");
        Require(matches.Append(static_cast<json::EncodedText&&>(error.detail), error) &&
            matches.Count() == 6 && matches.Text(5) == "diagnostic payload" && !error.detail.IsValid(),
            "matching payload lost its diagnostic input alias");
        matches.Clear(); matches.Clear(); paths.Clear(); paths.Clear();
        Require(!matches.Count() && !paths.Count(), "catalog owners retained cleared data");
    }
}

int main()
{
    ErrorOwnership();
    DecoderFailures();
    CatalogOwners();
    SettingsSnapshotError error;
    DecodedSettings owner;
    for (int byte = 255; byte >= 0; --byte)
    {
        char name[] = {char(byte), 'k'};
        char value[] = {'v', char(byte), '\0', 'x'};
        Require(owner.Insert({name, 2}, {value, 4}, error), "cannot insert byte control");
    }
    Require(owner.Count() == 256, "byte controls lost entries");
    for (size_t index = 0; index < owner.Count(); ++index)
    {
        const auto& row = owner.Entries()[index];
        Require(row.name.size() == 2 && static_cast<unsigned char>(row.name[0]) == index &&
            row.value.size() == 4 && static_cast<unsigned char>(row.value[1]) == index && row.value[2] == '\0',
            "owned bytes or unsigned name order changed");
    }
    const auto first = owner.Entries()[0];
    Require(!owner.Insert(first.name, "duplicate", error) && error.code == SettingsSnapshotErrorCode::Duplicate &&
        owner.Find(first.name)->value == first.value, "duplicate insert changed the original");
    Require(owner.Set(first.name, first.value, error), "self-borrowed replacement failed");

    DecodedSettings output;
    Require(output.Insert("old", "value", error), "cannot prepare clone destination");
    const auto* oldEntries = output.Entries();
    size_t cloneFailures = 0;
    for (size_t failure = 0; failure != 300; ++failure)
    {
        FailSettingsSnapshotAllocationAfter(failure);
        const bool accepted = owner.CloneTo(output, error);
        ClearSettingsSnapshotAllocationFailure();
        if (accepted) break;
        ++cloneFailures;
        Require(error.code == SettingsSnapshotErrorCode::OutOfMemory && output.Entries() == oldEntries &&
            output.Count() == 1 && output.Find("old")->value == "value", "failed clone changed its destination");
    }
    Require(cloneFailures == 257 && output.Count() == 256 &&
        output.Entries()[0].name.data() != owner.Entries()[0].name.data(), "clone did not own independent text");
    Require(owner.CloneTo(owner, error), "self-clone failed");
    Require(output.Set(output.Entries()[0].name, "changed", error) &&
        owner.Entries()[0].value != "changed", "clone replacement changed its source");
    Require(output.Erase(output.Entries()[0].name) && output.Count() == 255 && owner.Count() == 256,
        "erase lost independent ownership");

    DecodedSettings growing;
    for (char byte = '0'; byte != '8'; ++byte)
        Require(growing.Insert({&byte, 1}, "payload", error), "cannot prepare capacity control");
    const auto* entries = growing.Entries();
    const auto borrowedName = growing.Find("0")->value;
    const auto borrowedValue = growing.Find("1")->name;
    for (size_t failure = 0; failure != 2; ++failure)
    {
        FailSettingsSnapshotAllocationAfter(failure);
        const bool accepted = growing.Insert(borrowedName, borrowedValue, error);
        ClearSettingsSnapshotAllocationFailure();
        Require(!accepted && error.code == SettingsSnapshotErrorCode::OutOfMemory && growing.Entries() == entries &&
            growing.Count() == 8 && growing.Find("0")->value == "payload", "failed insertion changed owner or views");
    }
    Require(growing.Insert(borrowedName, borrowedValue, error) && growing.Find("payload")->value == "1",
        "insertion retry or aliased source failed");
    entries = growing.Entries();
    FailSettingsSnapshotAllocationAfter(0);
    const bool replaced = growing.Set("payload", "changed", error);
    ClearSettingsSnapshotAllocationFailure();
    Require(!replaced && growing.Entries() == entries && growing.Find("payload")->value == "1",
        "failed replacement changed owner or value");
    Require(growing.Set("payload", "changed", error), "replacement retry failed");
    Require(!growing.Insert({nullptr, 1}, "x", error) &&
        !growing.Insert({reinterpret_cast<const char*>(UINTPTR_MAX - 2), 8}, "x", error) &&
        growing.Entries() == entries && growing.Count() == 9, "invalid spans changed the owner");
    Require(growing.Insert({}, {}, error) && growing.Find({}) && growing.Find({})->value.empty(),
        "empty owned names or values were narrowed");
    DecodedSettings moved(static_cast<DecodedSettings&&>(growing));
    Require(growing.Count() == 0 && moved.Find("payload")->value == "changed", "move construction lost the owner");
    output = static_cast<DecodedSettings&&>(moved);
    Require(moved.Count() == 0 && output.Find("payload")->value == "changed", "move assignment lost the owner");
    output = static_cast<DecodedSettings&&>(output);
    Require(output.Find("payload")->value == "changed", "self-move changed the owner");
    output.Clear(); output.Clear();
    Require(output.Count() == 0 && !output.Find("payload"), "clear retained entries");
    printf("storage passed: 256 byte rows, %zu clone failures, insert/replace retries, aliases and moves\n", cloneFailures);
    return 0;
}
