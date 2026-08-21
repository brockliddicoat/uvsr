#include "ui_commands.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "UI command validation failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    uvsr::UiCommand RequireSuccess(
        const uvsr::UiCommandParseResult& result,
        const char* message)
    {
        Require(result.Succeeded(), message);
        Require(result.message.empty(),
            "a successful parse must not retain an error message");
        Require(result.errorOffset == uvsr::UiCommandNoOffset,
            "a successful parse must not retain an error offset");
        return result.command;
    }

    void RequireError(
        std::string_view input,
        uvsr::UiCommandParseError expected,
        const char* message)
    {
        const uvsr::UiCommandParseResult result =
            uvsr::ParseUiCommand(input);
        Require(!result.Succeeded(), message);
        Require(result.error == expected, message);
        Require(!result.message.empty(),
            "a parse error must include a useful message");
        Require(result.errorOffset != uvsr::UiCommandNoOffset,
            "a parse error must identify its source offset");
        Require(result.command.path.empty() &&
                result.command.action.empty() &&
                result.command.arguments.empty(),
            "a parse error must not expose a dispatchable payload");
    }
}

int main()
{
    using namespace uvsr;

    static_assert(UiFontFamilyCommandPath == "ui.font-family");

    {
        const UiCommandParseResult result =
            ParseUiCommand(" \t /SeT  renderer.mode  \"deferred path\"  ");
        const UiCommand& command = RequireSuccess(
            result,
            "leading/trailing whitespace and mixed-case verbs must parse");
        Require(command.verb == UiCommandVerb::Set,
            "set must normalize to its canonical verb");
        Require(command.path == "renderer.mode",
            "set must retain its engineering path");
        Require(command.arguments ==
                std::vector<std::string>{ "deferred path" },
            "a quoted multiword value must remain one argument");
    }

    {
        const UiCommandParseResult result = ParseUiCommand(
            R"(set scene.path "C:\\Scenes\\Sponza Main.json" alpha\ beta 'gamma delta' "")");
        const UiCommand& command = RequireSuccess(
            result,
            "quoted and escaped values must tokenize");
        Require(
            command.arguments ==
                std::vector<std::string>({
                    R"(C:\Scenes\Sponza Main.json)",
                    "alpha beta",
                    "gamma delta",
                    ""
                }),
            "quotes, escaped backslashes, escaped spaces, and empty values must decode");
    }

    {
        const UiCommandParseResult result =
            ParseUiCommand(R"(/set label "a \"quoted\" value" one\\two)");
        const UiCommand& command = RequireSuccess(
            result,
            "escaped quotes and slashes must parse");
        Require(
            command.arguments ==
                std::vector<std::string>({
                    "a \"quoted\" value",
                    R"(one\two)"
                }),
            "escaping must preserve the escaped character exactly");
    }

    {
        const UiCommand& help = RequireSuccess(
            ParseUiCommand("/help"),
            "help without a topic must parse");
        Require(help.verb == UiCommandVerb::Help &&
                help.arguments.empty(),
            "help without a topic must have no arguments");

        const UiCommand& helpTopic = RequireSuccess(
            ParseUiCommand("help camera"),
            "help with a topic must parse");
        Require(helpTopic.verb == UiCommandVerb::Help &&
                helpTopic.arguments ==
                    std::vector<std::string>{ "camera" },
            "help must retain its optional topic");

        const UiCommand& list = RequireSuccess(
            ParseUiCommand("/list renderer."),
            "list with a prefix must parse");
        Require(list.verb == UiCommandVerb::List &&
                list.arguments ==
                    std::vector<std::string>{ "renderer." },
            "list must retain its optional prefix");

        const UiCommand& get = RequireSuccess(
            ParseUiCommand("/get renderer.mode"),
            "get must parse");
        Require(get.verb == UiCommandVerb::Get &&
                get.path == "renderer.mode",
            "get must expose a canonical path");

        const UiCommand& toggle = RequireSuccess(
            ParseUiCommand("/toggle ui.visible"),
            "toggle must parse");
        Require(toggle.verb == UiCommandVerb::Toggle &&
                toggle.path == "ui.visible",
            "toggle must expose a canonical path");

        const UiCommand& reset = RequireSuccess(
            ParseUiCommand("/reset all"),
            "reset all must parse");
        Require(reset.verb == UiCommandVerb::Reset &&
                reset.path == "all",
            "reset all must retain its explicit reset target");

        const UiCommand& run = RequireSuccess(
            ParseUiCommand("/run screenshot \"comparison frame\""),
            "run with arguments must parse");
        Require(run.verb == UiCommandVerb::Run &&
                run.action == "screenshot" &&
                run.arguments ==
                    std::vector<std::string>{ "comparison frame" },
            "run must separate its action from trailing arguments");

        const UiCommand& fontFamily = RequireSuccess(
            ParseUiCommand("/set ui.font-family noto-sans"),
            "font-family setting must use the generic Settings command");
        Require(
            fontFamily.verb == UiCommandVerb::Set &&
                fontFamily.path == UiFontFamilyCommandPath &&
                fontFamily.arguments ==
                    std::vector<std::string>{ "noto-sans" },
            "font-family setting must retain its canonical path and value");
    }

    {
        const UiCommand& skinGet = RequireSuccess(
            ParseUiCommand("/skin"),
            "skin without a value must parse");
        Require(skinGet.verb == UiCommandVerb::Get &&
                skinGet.path == UiSkinCommandPath,
            "skin without a value must desugar to get ui.skin");

        const UiCommand& skinSet = RequireSuccess(
            ParseUiCommand("/SKIN OG"),
            "skin with a value must parse");
        Require(skinSet.verb == UiCommandVerb::Set &&
                skinSet.path == UiSkinCommandPath &&
                skinSet.arguments ==
                    std::vector<std::string>{ "OG" },
            "skin with a value must desugar to set ui.skin");

        const UiCommand& uiToggle = RequireSuccess(
            ParseUiCommand("/ui"),
            "ui without a value must parse");
        Require(uiToggle.verb == UiCommandVerb::Toggle &&
                uiToggle.path == UiVisibilityCommandPath,
            "ui without a value must toggle Settings visibility");

        const UiCommand& explicitUiToggle = RequireSuccess(
            ParseUiCommand("/ui TOGGLE"),
            "ui toggle must parse case-insensitively");
        Require(explicitUiToggle.verb == UiCommandVerb::Toggle &&
                explicitUiToggle.path == UiVisibilityCommandPath,
            "ui toggle must desugar to the canonical toggle");

        const UiCommand& uiSet = RequireSuccess(
            ParseUiCommand("/ui show"),
            "ui with a state must parse");
        Require(uiSet.verb == UiCommandVerb::Set &&
                uiSet.path == UiVisibilityCommandPath &&
                uiSet.arguments == std::vector<std::string>{ "show" },
            "ui with a state must desugar to set ui.visible");

        const UiCommand& sceneGet = RequireSuccess(
            ParseUiCommand("/scene"),
            "scene without a value must parse");
        Require(sceneGet.verb == UiCommandVerb::Get &&
                sceneGet.path == UiSceneCommandPath,
            "scene without a value must desugar to get scene.current");

        const UiCommand& sceneSet = RequireSuccess(
            ParseUiCommand("/scene \"Sponza Main\""),
            "scene with a value must parse");
        Require(sceneSet.verb == UiCommandVerb::Set &&
                sceneSet.path == UiSceneCommandPath &&
                sceneSet.arguments ==
                    std::vector<std::string>{ "Sponza Main" },
            "scene with a value must desugar to set scene.current");

        const UiCommand& cameraSet = RequireSuccess(
            ParseUiCommand("/camera first-person"),
            "camera with a value must parse");
        Require(cameraSet.verb == UiCommandVerb::Set &&
                cameraSet.path == UiCameraCommandPath &&
                cameraSet.arguments ==
                    std::vector<std::string>{ "first-person" },
            "camera must desugar to set camera.mode");

        const UiCommand& locationGet = RequireSuccess(
            ParseUiCommand("/camera-location"),
            "camera-location without values must parse");
        Require(locationGet.verb == UiCommandVerb::Get &&
                locationGet.path == UiCameraLocationCommandPath,
            "camera-location must desugar to get camera.location");

        const UiCommand& locationSet = RequireSuccess(
            ParseUiCommand("/camera-location 1 2 3"),
            "camera-location with values must parse");
        Require(locationSet.verb == UiCommandVerb::Set &&
                locationSet.path == UiCameraLocationCommandPath &&
                locationSet.arguments ==
                    std::vector<std::string>({ "1", "2", "3" }),
            "camera-location values must remain separate for validation");

        const UiCommand& reload = RequireSuccess(
            ParseUiCommand("/reload-shaders changed"),
            "reload-shaders must parse");
        Require(reload.verb == UiCommandVerb::Run &&
                reload.action == UiReloadShadersCommandAction &&
                reload.arguments ==
                    std::vector<std::string>{ "changed" },
            "reload-shaders must desugar to a canonical run action");
    }

    RequireError("", UiCommandParseError::EmptyInput,
        "empty input must be rejected");
    RequireError(" \t ", UiCommandParseError::EmptyInput,
        "whitespace-only input must be rejected");
    RequireError(" / ", UiCommandParseError::MissingVerb,
        "a slash without a verb must be rejected");
    RequireError("/unknown value", UiCommandParseError::UnknownVerb,
        "unknown verbs must be rejected");
    RequireError("/get", UiCommandParseError::MissingArgument,
        "get without a path must be rejected");
    RequireError("/get one two", UiCommandParseError::TooManyArguments,
        "get with two paths must be rejected");
    RequireError("/set renderer.mode", UiCommandParseError::MissingArgument,
        "set without a value must be rejected");
    RequireError("/toggle", UiCommandParseError::MissingArgument,
        "toggle without a path must be rejected");
    RequireError("/reset one two", UiCommandParseError::TooManyArguments,
        "reset with multiple paths must be rejected");
    RequireError("/run", UiCommandParseError::MissingArgument,
        "run without an action must be rejected");
    RequireError("/help one two", UiCommandParseError::TooManyArguments,
        "help with multiple topics must be rejected");
    RequireError("/list one two", UiCommandParseError::TooManyArguments,
        "list with multiple prefixes must be rejected");
    RequireError(R"(/set "" value)", UiCommandParseError::EmptyArgument,
        "an explicitly empty path must be rejected");
    RequireError(R"(/run "")", UiCommandParseError::EmptyArgument,
        "an explicitly empty action must be rejected");
    RequireError(R"(/set path "open)", UiCommandParseError::UnterminatedQuote,
        "an unterminated quote must be rejected");
    RequireError("/set path value\\", UiCommandParseError::DanglingEscape,
        "a dangling escape must be rejected");
    RequireError("/\xC3\xBCnknown", UiCommandParseError::UnknownVerb,
        "a non-ASCII verb must be rejected");

    {
        const std::string input = " /Se";
        const UiCommandCompletionToken completion =
            GetUiCommandCompletionToken(input, input.size());
        Require(completion.target == UiCommandCompletionTarget::Verb &&
                completion.tokenIndex == 0u &&
                completion.replaceBegin == 2u &&
                completion.replaceEnd == input.size() &&
                completion.prefix == "Se",
            "verb completion must exclude the slash and retain typed casing");

        const std::vector<std::string_view> matches =
            CompleteUiCommandVerbs("/S");
        Require(matches ==
                std::vector<std::string_view>({
                    "set",
                    "skin",
                    "scene"
                }),
            "verb completion must include canonical verbs and aliases in stable order");
        Require(UiCommandCompletionMatches(
                "camera-location",
                "CAM"),
            "completion prefix matching must normalize ASCII case");
        Require(!UiCommandCompletionMatches(
                "camera-location",
                "scene"),
            "completion prefix matching must reject unrelated candidates");
    }

    {
        const std::string pathInput = "/set renderer.ali";
        const UiCommandCompletionToken path =
            GetUiCommandCompletionToken(pathInput, pathInput.size());
        Require(path.target == UiCommandCompletionTarget::Path &&
                path.tokenIndex == 1u &&
                path.prefix == "renderer.ali",
            "set's first operand must request path completion");

        const std::string valueInput =
            R"(/set renderer.mode "def)";
        const UiCommandCompletionToken value =
            GetUiCommandCompletionToken(valueInput, valueInput.size());
        Require(value.target == UiCommandCompletionTarget::Value &&
                value.tokenIndex == 2u &&
                value.prefix == "def" &&
                value.valuePath == "renderer.mode" &&
                value.replaceBegin == valueInput.find('"'),
            "incomplete quoted values must retain their Settings path");

        const std::string runInput = "/run screenshot final";
        const UiCommandCompletionToken argument =
            GetUiCommandCompletionToken(runInput, runInput.size());
        Require(argument.target == UiCommandCompletionTarget::Argument &&
                argument.tokenIndex == 2u &&
                argument.prefix == "final",
            "run operands after the action must request argument completion");

        const std::string actionInput = "/run reload";
        const UiCommandCompletionToken action =
            GetUiCommandCompletionToken(actionInput, actionInput.size());
        Require(action.target == UiCommandCompletionTarget::Action &&
                action.tokenIndex == 1u &&
                action.prefix == "reload",
            "run's first operand must request action completion");

        const std::string helpInput = "/help cam";
        Require(
            GetUiCommandCompletionToken(
                helpInput,
                helpInput.size()).target ==
                UiCommandCompletionTarget::HelpTopic,
            "help operands must request topic completion");

        const std::string listInput = "/list render";
        Require(
            GetUiCommandCompletionToken(
                listInput,
                listInput.size()).target ==
                UiCommandCompletionTarget::ListPrefix,
            "list operands must request path-prefix completion");

        const std::string trailingSpace = "/set renderer.mode ";
        const UiCommandCompletionToken emptyValue =
            GetUiCommandCompletionToken(
                trailingSpace,
                trailingSpace.size());
        Require(emptyValue.target == UiCommandCompletionTarget::Value &&
                emptyValue.tokenIndex == 2u &&
                emptyValue.prefix.empty() &&
                emptyValue.valuePath == "renderer.mode" &&
                emptyValue.replaceBegin == trailingSpace.size() &&
                emptyValue.replaceEnd == trailingSpace.size(),
            "trailing whitespace must retain the zero-width value context");

        const UiCommandCompletionToken skinValue =
            GetUiCommandCompletionToken("/skin ", 6u);
        const std::string fontFamilyInput =
            "/set ui.font-family ";
        const UiCommandCompletionToken fontFamilyValue =
            GetUiCommandCompletionToken(
                fontFamilyInput,
                fontFamilyInput.size());
        const UiCommandCompletionToken sceneValue =
            GetUiCommandCompletionToken("/scene ", 7u);
        const UiCommandCompletionToken cameraValue =
            GetUiCommandCompletionToken("/camera ", 8u);
        const UiCommandCompletionToken uiValue =
            GetUiCommandCompletionToken("/ui ", 4u);
        const UiCommandCompletionToken cameraLocationValue =
            GetUiCommandCompletionToken("/camera-location ", 17u);
        Require(
            skinValue.target == UiCommandCompletionTarget::Value &&
                skinValue.valuePath == "ui.skin" &&
                fontFamilyValue.target == UiCommandCompletionTarget::Value &&
                fontFamilyValue.valuePath == UiFontFamilyCommandPath &&
                uiValue.valuePath == "ui.visible" &&
                sceneValue.valuePath == "scene.current" &&
                cameraValue.valuePath == "camera.mode" &&
                cameraLocationValue.valuePath == "camera.location",
            "value aliases must resolve to their canonical Settings paths");
    }

    std::cout << "UI command validation passed\n";
    return EXIT_SUCCESS;
}
