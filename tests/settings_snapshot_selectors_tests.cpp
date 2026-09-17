#include "settings_snapshot_selectors.h"

#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

using namespace uvsr;

namespace
{
    unsigned checks = 0;
    void Require(bool accepted, const char* reason)
    {
        ++checks;
        if (!accepted) { std::fprintf(stderr, "%s\n", reason); std::exit(1); }
    }
    UiSettingsValue Seed()
    {
        UiSettingsValue value;
        SettingsSnapshotError error;
        Require(value.SetSelector("previous owned selector", error), "selector fixture allocation failed");
        return value;
    }
    bool Empty(const SettingsSnapshotAdapterOption& o) { return o.index == -1 && o.name.empty(); }
    bool Empty(const SettingsSnapshotSceneOption& o) { return o.fileName.empty() && o.displayName.empty(); }
    bool Empty(const SettingsSnapshotLightOption& o) { return o.index == 0 && o.identity.empty(); }
    bool Empty(const SettingsSnapshotMaterialOption& o) { return o.id == 0 && o.name.empty() && o.selectable; }
    template<class Option> struct Source
    {
        const Option* rows;
        size_t count;
        size_t failAt = SIZE_MAX;
        bool emptyError = false;
        mutable size_t calls = 0;
        SettingsSnapshotOptionSource<Option> View() const noexcept
        {
            return {this, count, [](const void* context, size_t index, Option& option, SettingsSnapshotError& error) noexcept {
                const auto& self = *static_cast<const Source*>(context);
                Require(Empty(option), "source reader received stale option storage");
                ++self.calls;
                if (index == self.failAt)
                {
                    if (!self.emptyError)
                        error = ComposeSettingsSnapshotError({std::string_view("source\0failure", 14)}, SettingsSnapshotErrorCode::Path, 41, 42);
                    return false;
                }
                option = self.rows[index];
                return true;
            }};
        }
    };
    void RequireSourceFailure(const UiSettingsValue& value, std::string_view prior, const SettingsSnapshotError& error)
    {
        Require(value.Text().data() == prior.data() && value.Text() == prior &&
            error.code == SettingsSnapshotErrorCode::Path && error.nativeCode == 41 && error.cleanupCode == 42 &&
            error.MessageView() == std::string_view("source\0failure", 14), "source failure lost outputs or structured diagnostic bytes");
    }
    void SourceFailures()
    {
        SettingsSnapshotError error;
        auto value = Seed();
        const auto prior = value.Text();
        const SettingsSnapshotAdapterOption adapters[] = {{3,"same"},{4,"later"}};
        Source<SettingsSnapshotAdapterOption> adapterSource{adapters,2,1};
        int64_t adapter = 91;
        Require(!ResolveSettingsSnapshotAdapterToken("same", adapterSource.View(), adapter, value, error) && adapter == 91,
            "adapter callback failure published an earlier match");
        RequireSourceFailure(value, prior, error);
        const SettingsSnapshotSceneOption scenes[] = {{"one.scene.json","same"},{"two.scene.json","later"}};
        Source<SettingsSnapshotSceneOption> sceneSource{scenes,2,1};
        size_t ordinal = 92;
        Require(!ResolveSettingsSnapshotSceneToken("same", sceneSource.View(), ordinal, value, error) && ordinal == 92,
            "scene callback failure published an ordinal");
        RequireSourceFailure(value, prior, error);
        const SettingsSnapshotLightOption lights[] = {{3,"same"},{4,"later"}};
        Source<SettingsSnapshotLightOption> lightSource{lights,2,1};
        size_t light = 93;
        Require(!ResolveSettingsSnapshotLightToken("same", lightSource.View(), light, value, error) && light == 93,
            "light callback failure published an earlier match");
        RequireSourceFailure(value, prior, error);
        const SettingsSnapshotMaterialOption materials[] = {{3,"same"},{4,"later"}};
        Source<SettingsSnapshotMaterialOption> materialSource{materials,2,1};
        bool none = true;
        uint32_t material = 94;
        Require(!ResolveSettingsSnapshotMaterialToken("same", materialSource.View(), none, material, value, error) && none && material == 94,
            "material callback failure published an earlier match");
        RequireSourceFailure(value, prior, error);
        adapterSource.emptyError = true;
        Require(!ResolveSettingsSnapshotAdapterToken("same", adapterSource.View(), adapter, value, error) &&
            error.code == SettingsSnapshotErrorCode::InvalidInput && error.MessageView() == "selector source could not read an option" &&
            value.Text().data() == prior.data(), "unexplained source failure lost its fallback reason or prior output");
        SettingsSnapshotOptionSource<SettingsSnapshotAdapterOption> missingReader{nullptr,1,nullptr};
        Require(!ResolveSettingsSnapshotAdapterToken("same", missingReader, adapter, value, error) &&
            error.MessageView() == "selector source has no reader" && value.Text().data() == prior.data(), "missing reader was called or changed output");
    }
    void FormattingAndOwnedResults()
    {
        SettingsSnapshotError error;
        auto value = Seed();
        auto prior = value.Text();
        char name[257];
        std::memset(name, 'L', sizeof(name)-1);name[sizeof(name)-1] = '\0';
        const auto maximum = (std::numeric_limits<size_t>::max)();
        FailUiSettingsValueAllocationAfter(0);
        const bool formatted = FormatSettingsSnapshotLightToken(maximum, name, value, error);
        ClearUiSettingsValueAllocationFailure();
        Require(!formatted && error.code == SettingsSnapshotErrorCode::OutOfMemory && value.Text().data() == prior.data() && value.Text() == prior,
            "light format OOM changed its published output");
        FailUiSettingsValueAllocationAfter(1);
        const bool oneAllocation = FormatSettingsSnapshotLightToken(maximum, name, value, error);
        ClearUiSettingsValueAllocationFailure();
        char prefix[32];
        const int digits = std::snprintf(prefix, sizeof(prefix), "%zu:", maximum);
        Require(oneAllocation && digits > 0 && value.Text().substr(0, static_cast<size_t>(digits)) == prefix &&
            value.Text().substr(static_cast<size_t>(digits)) == name && error.code == SettingsSnapshotErrorCode::None && error.MessageView().empty(),
            "light format did not use one allocation or lost full index/name bytes");
        prior = value.Text();
        const SettingsSnapshotLightOption rows[] = {{7,name}};
        Source<SettingsSnapshotLightOption> source{rows,1};
        size_t index = 81;
        FailUiSettingsValueAllocationAfter(0);
        const bool resolved = ResolveSettingsSnapshotLightToken("7", source.View(), index, value, error);
        ClearUiSettingsValueAllocationFailure();
        Require(!resolved && error.code == SettingsSnapshotErrorCode::OutOfMemory && index == 81 && value.Text().data() == prior.data(),
            "light resolve OOM changed a scalar or canonical owner");
        Require(ResolveSettingsSnapshotLightToken("7", source.View(), index, value, error) && index == 7,
            "light resolve retry failed");
        std::memset(name, 'X', sizeof(name)-1);
        Require(value.Text().size() == 258 && value.Text().substr(0,2) == "7:" && value.Text()[2] == 'L' && value.Text().back() == 'L',
            "resolved light token borrowed source name storage");

        value = Seed();prior = value.Text();
        const SettingsSnapshotSceneOption scenes[] = {{"nested/scene/with/long/name.scene.json","target"}};
        Source<SettingsSnapshotSceneOption> sceneSource{scenes,1};
        size_t ordinal = 82;
        FailUiSettingsValueAllocationAfter(0);
        const bool sceneResolved = ResolveSettingsSnapshotSceneToken("target", sceneSource.View(), ordinal, value, error);
        ClearUiSettingsValueAllocationFailure();
        Require(!sceneResolved && ordinal == 82 && error.code == SettingsSnapshotErrorCode::OutOfMemory && value.Text().data() == prior.data(),
            "scene resolve OOM changed an ordinal or canonical owner");
        Require(ResolveSettingsSnapshotSceneToken("target", sceneSource.View(), ordinal, value, error) && ordinal == 0 &&
            value.Text() == scenes[0].fileName, "scene resolve retry failed");
        Require(!FormatSettingsSnapshotSceneToken("bad.json", value, error) && value.Text().empty() &&
            value.kind == UiSettingsValueKind::Selector && error.code == SettingsSnapshotErrorCode::InvalidInput,
            "invalid scene format lost its empty-selector contract");
        Require(!FormatSettingsSnapshotAdapterToken(-1, value, error) && value.Text().empty() && error.code == SettingsSnapshotErrorCode::InvalidInput,
            "invalid adapter format lost its empty-selector contract");
        error = ComposeSettingsSnapshotError({"nested/from/error.scene.json"});
        Require(FormatSettingsSnapshotSceneToken(error.MessageView(), value, error) && value.Text() == "nested/from/error.scene.json",
            "scene format invalidated its aliased error input");
        Require(FormatSettingsSnapshotLightToken(8, value.Text(), value, error) && value.Text() == "8:nested/from/error.scene.json",
            "light format invalidated its aliased output input");
    }
    void MaterialSelectionOwnership()
    {
        const auto maximum = (std::numeric_limits<uint32_t>::max)();
        const SettingsSnapshotMaterialOption rows[] = {{maximum,"same",false},{7,"same"},{8,"hidden",false}};
        Source<SettingsSnapshotMaterialOption> source{rows,3};
        auto value = Seed();
        SettingsSnapshotError error;
        bool none = true;
        uint32_t id = 99;
        Require(ResolveSettingsSnapshotMaterialToken("same", source.View(), none, id, value, error) &&
            !none && id == 7 && value.Text() == "7", "unselectable material caused false name ambiguity");
        Require(!ResolveSettingsSnapshotMaterialToken("hidden", source.View(), none, id, value, error) &&
            !none && id == 7 && value.Text() == "7", "unselectable material name became selectable");
        Require(!ResolveSettingsSnapshotMaterialToken("8", source.View(), none, id, value, error) &&
            !none && id == 7 && value.Text() == "7", "unselectable material id became selectable");
        const SettingsSnapshotMaterialOption fullDomain[] = {{maximum,"maximum"}};
        Source<SettingsSnapshotMaterialOption> fullSource{fullDomain,1};
        Require(ResolveSettingsSnapshotMaterialToken("4294967295", fullSource.View(), none, id, value, error) &&
            !none && id == maximum && value.Text() == "4294967295", "selection filtering narrowed the material id domain");
    }
    void CommandTextHelpers()
    {
        Require(ContainsNormalizedCommandAscii("root/Bistro_Interior-Retextured/suffix", "bistro interior+retextured", true) &&
            !ContainsNormalizedCommandAscii("bistro\tinteriorretextured", "bistrointeriorretextured", true),
            "normalized substring search changed separator or substring behavior");
        Require(ContainsNormalizedCommandAscii("aaaaab", "aaab") &&
            !ContainsNormalizedCommandAscii("aaaaab", "aaabc") &&
            ContainsNormalizedCommandAscii("anything", "- _+", true) &&
            ContainsNormalizedCommandAscii(std::string_view("a\0b",3), std::string_view("\0b",2)),
            "normalized substring overlap, empty needle or zero byte changed");
        int64_t value = -91;
        errno = EDOM;
        Require(!TryParseCommandInteger({}, value) && value == -91 && errno == EDOM,
            "empty integer changed output or errno");
        FailUiSettingsValueAllocationAfter(0);
        json::FailAllocationAfter(0);
        const bool parsed = TryParseCommandInteger(" \t+00017", value);
        const bool found = ContainsNormalizedCommandAscii("Bistro_Interior_Retextured", "bistro interior retextured", true);
        json::ClearAllocationFailure();
        ClearUiSettingsValueAllocationFailure();
        Require(parsed && value == 17 && errno == 0 && found, "command text helpers allocated or rejected ordinary input");
        Require(!TryParseCommandInteger("+-1", value) && value == 17 && errno == 0 &&
            !TryParseCommandInteger("17 ", value) && value == 17 && errno == 0,
            "integer grammar accepted two signs or trailing whitespace");
        Require(TryParseCommandInteger("-9223372036854775808", value) && value == (std::numeric_limits<int64_t>::min)() &&
            !TryParseCommandInteger("-9223372036854775809", value) && value == (std::numeric_limits<int64_t>::min)() && errno == ERANGE,
            "integer lower bound or overflow publication changed");
        Require(TryParseCommandInteger("9223372036854775807", value) && value == (std::numeric_limits<int64_t>::max)() &&
            !TryParseCommandInteger("9223372036854775808", value) && value == (std::numeric_limits<int64_t>::max)() && errno == ERANGE,
            "integer upper bound or overflow publication changed");
    }
    void NormalizedBytesAndShortPaths()
    {
        for (unsigned value = 0; value < 256; ++value)
        {
            const char byte = static_cast<char>(value);
            const char lower = value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : byte;
            Require(EqualNormalizedCommandAscii({&byte,1},{&lower,1}), "ASCII comparison changed an ordinary byte");
            const bool separator = byte == ' ' || byte == '-' || byte == '_' || byte == '+';
            Require(EqualNormalizedCommandAscii({&byte,1},{},true) == separator, "ASCII comparison skipped a nonseparator byte");
        }
        Require(EqualNormalizedCommandAscii(" A- B_C+D ", "abcd", true) &&
            !EqualNormalizedCommandAscii("a\tb", "ab", true) && !EqualNormalizedCommandAscii("a\nb", "ab", true),
            "normalized separator grammar changed");
        auto output = Seed();
        SettingsSnapshotError error;
        const SettingsSnapshotAdapterOption rows[] = {{3,"short"}};
        Source<SettingsSnapshotAdapterOption> source{rows,1};
        int64_t index = 90;
        FailUiSettingsValueAllocationAfter(0);
        json::FailAllocationAfter(0);
        const bool resolved = ResolveSettingsSnapshotAdapterToken("s-h_o+r t", source.View(), index, output, error);
        json::ClearAllocationFailure();
        ClearUiSettingsValueAllocationFailure();
        Require(resolved && index == 3 && output.Text() == "3" && error.code == SettingsSnapshotErrorCode::None,
            "short selector matching allocated normalized or result text");
    }
}
int main()
{
    SourceFailures();
    FormattingAndOwnedResults();
    MaterialSelectionOwnership();
    CommandTextHelpers();
    NormalizedBytesAndShortPaths();
    std::printf("selector ownership checks: %u passed\n", checks);
    return 0;
}
