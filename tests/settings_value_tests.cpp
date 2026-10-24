#include "settings_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;
    size_t checks = 0;

    void Require(bool passed, const char* message)
    {
        ++checks;
        if (!passed)
        {
            fprintf(stderr, "settings value failure: %s\n", message);
            exit(1);
        }
    }

    json::EncodedText Own(std::string_view value)
    {
        json::EncodedText result([](json::OutputWriter& writer, const void* context) noexcept {
            const auto text = *static_cast<const std::string_view*>(context);
            return writer.Raw({text.data(), text.size()});
        }, &value);
        Require(result.IsValid(), "fixture text allocation");
        return result;
    }

    const UiSettingsCommandDefinition& Definition(SettingId id)
    {
        for (const auto& definition : UiSettingsCommandCatalog)
            if (definition.id == id) return definition;
        Require(false, "fixture setting exists");
        return UiSettingsCommandCatalog[0];
    }

    void StorageAndFailure()
    {
        char bytes[514];
        memset(bytes, 'x', sizeof(bytes));
        bytes[513] = '\0';
        const size_t lengths[] = {0, 15, 16, 513};
        for (size_t length : lengths)
        {
            SettingsSnapshotError error;
            UiSettingsValue value;
            Require(value.SetSelector("previous independently owned selector", error), "initial selector");
            const auto previous = value.Text();
            FailUiSettingsValueAllocationAfter(0);
            const bool assigned = value.SetToken({bytes, length}, error);
            ClearUiSettingsValueAllocationFailure();
            Require(assigned == (length < 16), "short text does not allocate; long text reports exhaustion");
            if (!assigned)
            {
                Require(error.code == SettingsSnapshotErrorCode::OutOfMemory, "assignment reports allocation failure");
                Require(value.kind == UiSettingsValueKind::Selector && value.Text().data() == previous.data() &&
                    value.Text() == "previous independently owned selector", "failed assignment retains kind, bytes and view identity");
            }
            Require(value.SetToken({bytes, length}, error), "text assignment succeeds");
            Require(value.Text() == std::string_view(bytes, length) && value.Text().data()[length] == '\0', "exact owned bytes and terminator");
            Require(value.kind == UiSettingsValueKind::Token, "text assignment publishes requested kind");
        }

        SettingsSnapshotError error;
        UiSettingsValue value;
        Require(value.SetToken("original long value with retained storage", error), "alias fixture");
        const auto previous = value.Text();
        FailUiSettingsValueAllocationAfter(0);
        Require(!value.SetSelector(previous.substr(1), error), "aliased long assignment can fail");
        ClearUiSettingsValueAllocationFailure();
        Require(value.Text().data() == previous.data() && value.Text() == "original long value with retained storage", "failed alias keeps original storage");
        Require(value.SetSelector(value.Text().substr(1), error), "long alias copies before publication");
        Require(value.Text() == "riginal long value with retained storage", "long alias bytes");
        Require(value.SetToken(value.Text().substr(0, 7), error) && value.Text() == "riginal", "long-to-short alias");
        Require(value.SetSelector(value.Text().substr(1), error) && value.Text() == "iginal", "short alias");

        error.detail = Own("diagnostic bytes borrowed during a checked assignment");
        Require(value.SetToken(error.MessageView(), error), "diagnostic alias survives error reset");
        Require(value.Text() == "diagnostic bytes borrowed during a checked assignment", "diagnostic alias bytes");
        error.detail = Own(std::string_view("one\0two", 7));
        Require(error.MessageView().size() == 7, "diagnostic view includes embedded zero");
        Require(value.SetSelector(error.MessageView(), error) && value.Text() == std::string_view("one\0two", 7), "owned byte ranges retain embedded zero");
    }

    void MovesAndClones()
    {
        SettingsSnapshotError error;
        for (const char* text : {"short", "long selector text for independent lifetime"})
        {
            UiSettingsValue source;
            Require(source.SetSelector(text, error), "move source");
            const auto original = source.Text();
            UiSettingsValue moved(static_cast<UiSettingsValue&&>(source));
            Require(moved.Text() == text && source.Text().empty(), "move construction transfers value and empties source text");
            if (original.size() >= 16) Require(moved.Text().data() == original.data(), "long move transfers allocation");
            Require(source.SetToken("reused source", error), "moved-from owner remains usable");
            UiSettingsValue assigned;
            Require(assigned.SetToken("old destination allocation that must be freed", error), "move destination");
            assigned = static_cast<UiSettingsValue&&>(moved);
            Require(assigned.Text() == text && moved.Text().empty(), "move assignment releases old storage");
            const auto live = assigned.Text();
            assigned = static_cast<UiSettingsValue&&>(assigned);
            Require(assigned.Text().data() == live.data() && assigned.Text() == text, "self move preserves owner");
            FailUiSettingsValueAllocationAfter(0);
            Require(assigned.CloneTo(assigned, error), "self clone needs no allocation");
            ClearUiSettingsValueAllocationFailure();

            UiSettingsValue clone;
            Require(clone.SetToken("published clone destination", error), "clone destination");
            const auto prior = clone.Text();
            FailUiSettingsValueAllocationAfter(0);
            const bool copied = assigned.CloneTo(clone, error);
            ClearUiSettingsValueAllocationFailure();
            Require(copied == (live.size() < 16), "clone reports required long allocation");
            if (!copied) Require(clone.Text().data() == prior.data() && clone.Text() == "published clone destination" &&
                clone.kind == UiSettingsValueKind::Token, "failed clone preserves destination");
            Require(assigned.CloneTo(clone, error) && clone == assigned, "clone preserves complete value");
            Require(clone.Text().data() != assigned.Text().data(), "clones have independent storage");
            Require(assigned.SetToken("changed", error) && clone.Text() == text, "clone survives source replacement");
        }
        UiSettingsValue arithmetic = UiSettingsValue::Vector({1.f, 2.f, 3.f, 4.f}, 3);
        UiSettingsValue copy;
        FailUiSettingsValueAllocationAfter(0);
        Require(arithmetic.CloneTo(copy, error) && copy == arithmetic, "arithmetic clone needs no allocation");
        ClearUiSettingsValueAllocationFailure();
    }

    void MultipartOwnership()
    {
        SettingsSnapshotError error;
        SettingsSnapshotText text;
        FailUiSettingsValueAllocationAfter(0);
        const bool empty = text.AssignParts({}, error);
        const bool shortText = text.AssignParts({"12345", "67890", "12345"}, error);
        ClearUiSettingsValueAllocationFailure();
        Require(empty && shortText && text.View() == "123456789012345", "multipart short text stays inline");
        const auto shortView = text.View();
        FailUiSettingsValueAllocationAfter(0);
        const bool failed = text.AssignParts({text.View(), "x"}, error);
        ClearUiSettingsValueAllocationFailure();
        Require(!failed && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            text.View().data() == shortView.data() && text.View() == shortView,
            "multipart threshold failure preserves an aliased inline owner");
        FailUiSettingsValueAllocationAfter(1);
        const bool joined = text.AssignParts({text.View(), "x", std::string_view("\0z", 2)}, error);
        ClearUiSettingsValueAllocationFailure();
        Require(joined && text.View() == std::string_view("123456789012345x\0z", 18),
            "multipart long text uses one allocation and preserves embedded zero");
        const auto longView = text.View();
        FailUiSettingsValueAllocationAfter(0);
        const bool longFailed = text.AssignParts({"prefix:", text.View(), text.View()}, error);
        ClearUiSettingsValueAllocationFailure();
        Require(!longFailed && text.View().data() == longView.data() && text.View() == longView,
            "multipart failure preserves aliased long storage");
        error.detail = Own("owned diagnostic input with full bytes");
        Require(text.AssignParts({"prefix:", error.MessageView(), ":suffix"}, error) &&
            text.View() == "prefix:owned diagnostic input with full bytes:suffix",
            "multipart assignment retains aliased error detail until all parts are copied");
        UiSettingsValue value;
        Require(value.SetToken("retained token owner", error), "multipart selector fixture");
        const auto prior = value.Text();
        FailUiSettingsValueAllocationAfter(0);
        const bool selectorFailed = value.SetSelectorParts({"1:", value.Text()}, error);
        ClearUiSettingsValueAllocationFailure();
        Require(!selectorFailed && value.kind == UiSettingsValueKind::Token &&
            value.Text().data() == prior.data() && value.Text() == prior,
            "multipart selector failure preserves kind and aliased text");
        Require(value.SetSelectorParts({"1:", value.Text()}, error) &&
            value.kind == UiSettingsValueKind::Selector && value.Text() == "1:retained token owner",
            "multipart selector publishes a complete owned value");
    }

    void PublicationAndErrors()
    {
        SettingsSnapshotError error;
        UiSettingsValue value;
        Require(value.SetSelector("previous owned selector output", error), "parse output fixture");
        const auto prior = value.Text();
        const auto& scene = Definition(SettingId::SceneCurrent);
        FailUiSettingsValueAllocationAfter(0);
        Require(!ParseCanonicalUiSettingsValue(scene, "nested/scene.scene.json", value, error), "parse propagates text exhaustion");
        ClearUiSettingsValueAllocationFailure();
        Require(error.code == SettingsSnapshotErrorCode::OutOfMemory && value.Text().data() == prior.data() &&
            value.Text() == "previous owned selector output", "parse allocation failure preserves output");
        Require(!ParseCanonicalUiSettingsValue(scene, "../scene.scene.json", value, error) &&
            value.Text().data() == prior.data(), "parse validation failure preserves output");
        Require(ParseCanonicalUiSettingsValue(scene, "nested/scene.scene.json", value, error), "valid selector publishes");
        Require(ParseCanonicalUiSettingsValue(scene, value.Text(), value, error), "parse handles output text alias");

        json::EncodedText canonical = Own("previous canonical output");
        const char* oldCanonical = canonical.Data();
        json::FailAllocationAfter(0);
        Require(!FormatUiSettingsValue(scene, value, canonical, error), "format propagates JSON allocation failure");
        json::ClearAllocationFailure();
        Require(error.code == SettingsSnapshotErrorCode::OutOfMemory && canonical.Data() == oldCanonical &&
            std::string_view(canonical.Data(), canonical.Size()) == "previous canonical output", "failed format preserves published output");
        Require(FormatUiSettingsValue(scene, value, canonical, error) &&
            std::string_view(canonical.Data(), canonical.Size()) == value.Text(), "successful format publishes complete bytes");

        SettingsSnapshotText readerText;
        Require(readerText.Assign("previous reader canonical bytes", error), "reader format fixture");
        const auto oldReaderText = readerText.View();
        FailUiSettingsValueAllocationAfter(0);
        const bool readerFormatted = FormatUiSettingsValue(scene, value, readerText, error);
        ClearUiSettingsValueAllocationFailure();
        Require(!readerFormatted && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            readerText.View().data() == oldReaderText.data() && readerText.View() == oldReaderText,
            "reader format allocation failure retains complete output and view identity");
        Require(FormatUiSettingsValue(scene, value, readerText, error) && readerText.View() == value.Text(),
            "reader format publishes the same canonical bytes");
        FailUiSettingsValueAllocationAfter(0);
        json::FailAllocationAfter(0);
        const bool shortFormatted = FormatUiSettingsValue(Definition(SettingId::SkyAmbientFillEnabled),
            UiSettingsValue::Boolean(true), readerText, error);
        json::ClearAllocationFailure();
        ClearUiSettingsValueAllocationFailure();
        Require(shortFormatted && readerText.View() == "on", "short canonical reader text needs no allocation");

        error.detail = Own("nested/scene.scene.json");
        Require(ParseCanonicalUiSettingsValue(scene, error.MessageView(), value, error) &&
            value.Text() == "nested/scene.scene.json", "parse retains aliased diagnostic input");

        const auto& noise = Definition(SettingId::NoisePattern);
        const auto unchanged = value.Text();
        FailUiSettingsValueAllocationAfter(0);
        const bool defaulted = GetDeclaredUiSettingsDefaultValue(noise, value, error);
        ClearUiSettingsValueAllocationFailure();
        Require(!defaulted && error.code == SettingsSnapshotErrorCode::OutOfMemory &&
            value.Text().data() == unchanged.data(), "long token default failure retains prior value");
        Require(GetDeclaredUiSettingsDefaultValue(noise, value, error), "default succeeds when allocation succeeds");
        json::FailAllocationAfter(0);
        Require(!ParseCanonicalUiSettingsValue(Definition(SettingId::ShadowsRayTracedSamplesPerPixel), "invalid", value, error), "diagnostic allocation failure is explicit");
        json::ClearAllocationFailure();
        Require(error.code == SettingsSnapshotErrorCode::OutOfMemory && value.Text() == "spatiotemporal-blue", "diagnostic failure preserves parsed output");

        float scalar = 123.5f;
        FailUiSettingsValueAllocationAfter(0);
        Require(!ParseCanonicalSettingsFloat("0.00000000000000000001", scalar, error), "float scratch allocation failure");
        ClearUiSettingsValueAllocationFailure();
        Require(scalar == 123.5f && error.code == SettingsSnapshotErrorCode::OutOfMemory, "float allocation failure preserves scalar");
    }
}

int main()
{
    StorageAndFailure();
    MovesAndClones();
    MultipartOwnership();
    PublicationAndErrors();
    printf("passed %zu settings value ownership and failure checks\n", checks);
    return 0;
}
