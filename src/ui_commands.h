#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace uvsr
{
    enum class UiCommandVerb
    {
        Help,
        List,
        Get,
        Set,
        Toggle,
        Reset,
        Run
    };

    enum class UiCommandParseError
    {
        None,
        EmptyInput,
        MissingVerb,
        UnknownVerb,
        UnterminatedQuote,
        DanglingEscape,
        MissingArgument,
        TooManyArguments,
        EmptyArgument
    };

    inline constexpr std::size_t UiCommandNoOffset =
        static_cast<std::size_t>(-1);

    inline constexpr std::string_view UiSkinCommandPath = "ui.skin";
    inline constexpr std::string_view UiFontFamilyCommandPath =
        "ui.font-family";
    inline constexpr std::string_view UiVisibilityCommandPath = "ui.visible";
    inline constexpr std::string_view UiSceneCommandPath = "scene.current";
    inline constexpr std::string_view UiCameraCommandPath = "camera.mode";
    inline constexpr std::string_view UiReloadShadersCommandAction =
        "reload-shaders";
    inline constexpr std::string_view UiSettingsLoadCommandAction =
        "settings.load";

    struct UiCommand
    {
        UiCommandVerb verb = UiCommandVerb::Help;

        // Exactly one of path and action is populated for commands that need a
        // dispatch target. Help/list keep both empty. Set values and run
        // arguments remain tokenized so the renderer-facing validator can apply
        // type and range rules without reparsing user input.
        std::string path;
        std::string action;
        std::vector<std::string> arguments;
    };

    struct UiCommandParseResult
    {
        UiCommand command;
        UiCommandParseError error = UiCommandParseError::None;
        std::size_t errorOffset = UiCommandNoOffset;
        std::string message;

        [[nodiscard]] bool Succeeded() const
        {
            return error == UiCommandParseError::None;
        }

        explicit operator bool() const
        {
            return Succeeded();
        }
    };

    [[nodiscard]] constexpr std::string_view UiCommandVerbName(
        UiCommandVerb verb)
    {
        switch (verb)
        {
        case UiCommandVerb::Help:
            return "help";
        case UiCommandVerb::List:
            return "list";
        case UiCommandVerb::Get:
            return "get";
        case UiCommandVerb::Set:
            return "set";
        case UiCommandVerb::Toggle:
            return "toggle";
        case UiCommandVerb::Reset:
            return "reset";
        case UiCommandVerb::Run:
            return "run";
        }
        return {};
    }

    inline constexpr std::array<std::string_view, 13>
        UiCommandVerbCompletionCandidates = {
            "help",
            "list",
            "get",
            "set",
            "toggle",
            "reset",
            "run",
            "skin",
            "ui",
            "scene",
            "camera",
            "reload-shaders",
            "settings.load"
        };

    namespace detail
    {
        [[nodiscard]] constexpr bool IsUiCommandWhitespace(char character)
        {
            return character == ' ' ||
                character == '\t' ||
                character == '\r' ||
                character == '\n' ||
                character == '\f' ||
                character == '\v';
        }

        [[nodiscard]] inline bool NormalizeUiCommandAscii(
            std::string_view text,
            std::string& normalized)
        {
            normalized.clear();
            normalized.reserve(text.size());

            for (const unsigned char character : text)
            {
                if (character > 0x7fu)
                {
                    normalized.clear();
                    return false;
                }
                if (character >= static_cast<unsigned char>('A') &&
                    character <= static_cast<unsigned char>('Z'))
                {
                    normalized.push_back(static_cast<char>(
                        character - static_cast<unsigned char>('A') +
                        static_cast<unsigned char>('a')));
                }
                else
                {
                    normalized.push_back(static_cast<char>(character));
                }
            }
            return true;
        }

        struct UiCommandLexToken
        {
            std::string text;
            std::size_t begin = 0u;
            std::size_t end = 0u;
        };

        struct UiCommandLexResult
        {
            std::vector<UiCommandLexToken> tokens;
            UiCommandParseError error = UiCommandParseError::None;
            std::size_t errorOffset = UiCommandNoOffset;
            std::string message;
            std::size_t commandBegin = 0u;
            bool hadLeadingSlash = false;
        };

        [[nodiscard]] inline UiCommandLexResult LexUiCommand(
            std::string_view input,
            bool allowIncomplete)
        {
            UiCommandLexResult result;
            std::size_t cursor = 0u;
            while (cursor < input.size() &&
                IsUiCommandWhitespace(input[cursor]))
            {
                ++cursor;
            }

            if (cursor < input.size() && input[cursor] == '/')
            {
                result.hadLeadingSlash = true;
                ++cursor;
            }
            result.commandBegin = cursor;

            while (cursor < input.size())
            {
                while (cursor < input.size() &&
                    IsUiCommandWhitespace(input[cursor]))
                {
                    ++cursor;
                }
                if (cursor >= input.size())
                    break;

                UiCommandLexToken token;
                token.begin = cursor;
                char quote = '\0';
                std::size_t quoteOffset = UiCommandNoOffset;
                bool escaping = false;
                std::size_t escapeOffset = UiCommandNoOffset;

                while (cursor < input.size())
                {
                    const char character = input[cursor];
                    if (escaping)
                    {
                        token.text.push_back(character);
                        escaping = false;
                        ++cursor;
                        continue;
                    }
                    if (character == '\\')
                    {
                        escaping = true;
                        escapeOffset = cursor;
                        ++cursor;
                        continue;
                    }
                    if (quote != '\0')
                    {
                        if (character == quote)
                            quote = '\0';
                        else
                            token.text.push_back(character);
                        ++cursor;
                        continue;
                    }
                    if (character == '"' || character == '\'')
                    {
                        quote = character;
                        quoteOffset = cursor;
                        ++cursor;
                        continue;
                    }
                    if (IsUiCommandWhitespace(character))
                        break;

                    token.text.push_back(character);
                    ++cursor;
                }

                token.end = cursor;
                result.tokens.push_back(std::move(token));

                if (escaping && !allowIncomplete)
                {
                    result.error = UiCommandParseError::DanglingEscape;
                    result.errorOffset = escapeOffset;
                    result.message =
                        "A trailing backslash must escape another character.";
                    return result;
                }
                if (quote != '\0' && !allowIncomplete)
                {
                    result.error = UiCommandParseError::UnterminatedQuote;
                    result.errorOffset = quoteOffset;
                    result.message =
                        "The quoted argument is missing its closing quote.";
                    return result;
                }
            }

            return result;
        }

        [[nodiscard]] inline UiCommandParseResult MakeUiCommandError(
            UiCommandParseError error,
            std::size_t offset,
            std::string message)
        {
            UiCommandParseResult result;
            result.error = error;
            result.errorOffset = offset;
            result.message = std::move(message);
            return result;
        }

        [[nodiscard]] inline UiCommandParseResult MakeUiCommandSuccess(
            UiCommand command)
        {
            UiCommandParseResult result;
            result.command = std::move(command);
            return result;
        }

        inline void CopyArguments(
            const std::vector<UiCommandLexToken>& tokens,
            std::size_t first,
            UiCommand& command)
        {
            command.arguments.reserve(tokens.size() - first);
            for (std::size_t index = first; index < tokens.size(); ++index)
                command.arguments.push_back(tokens[index].text);
        }

        [[nodiscard]] inline UiCommand MakePathAlias(
            std::string_view path,
            const std::vector<UiCommandLexToken>& tokens)
        {
            UiCommand command;
            command.verb = tokens.size() == 1u
                ? UiCommandVerb::Get
                : UiCommandVerb::Set;
            command.path.assign(path.data(), path.size());
            if (tokens.size() > 1u)
                CopyArguments(tokens, 1u, command);
            return command;
        }

        [[nodiscard]] inline std::string DecodeCompletionPrefix(
            std::string_view raw)
        {
            std::string decoded;
            decoded.reserve(raw.size());
            char quote = '\0';
            bool escaping = false;

            for (const char character : raw)
            {
                if (escaping)
                {
                    decoded.push_back(character);
                    escaping = false;
                }
                else if (character == '\\')
                {
                    escaping = true;
                }
                else if (quote != '\0')
                {
                    if (character == quote)
                        quote = '\0';
                    else
                        decoded.push_back(character);
                }
                else if (character == '"' || character == '\'')
                {
                    quote = character;
                }
                else
                {
                    decoded.push_back(character);
                }
            }

            return decoded;
        }
    }

    [[nodiscard]] inline UiCommandParseResult ParseUiCommand(
        std::string_view input)
    {
        detail::UiCommandLexResult lexed =
            detail::LexUiCommand(input, false);
        if (lexed.error != UiCommandParseError::None)
        {
            return detail::MakeUiCommandError(
                lexed.error,
                lexed.errorOffset,
                std::move(lexed.message));
        }
        if (lexed.tokens.empty())
        {
            return detail::MakeUiCommandError(
                lexed.hadLeadingSlash
                    ? UiCommandParseError::MissingVerb
                    : UiCommandParseError::EmptyInput,
                lexed.commandBegin,
                lexed.hadLeadingSlash
                    ? "Expected a command verb after '/'."
                    : "Expected a command.");
        }

        std::string verb;
        if (!detail::NormalizeUiCommandAscii(
                lexed.tokens.front().text,
                verb))
        {
            verb.clear();
        }

        if (verb == "skin")
        {
            return detail::MakeUiCommandSuccess(
                detail::MakePathAlias(UiSkinCommandPath, lexed.tokens));
        }
        if (verb == "scene")
        {
            return detail::MakeUiCommandSuccess(
                detail::MakePathAlias(UiSceneCommandPath, lexed.tokens));
        }
        if (verb == "camera")
        {
            return detail::MakeUiCommandSuccess(
                detail::MakePathAlias(UiCameraCommandPath, lexed.tokens));
        }
        if (verb == "ui")
        {
            UiCommand command;
            command.path.assign(
                UiVisibilityCommandPath.data(),
                UiVisibilityCommandPath.size());

            std::string firstArgument;
            const bool explicitToggle =
                lexed.tokens.size() == 2u &&
                detail::NormalizeUiCommandAscii(
                    lexed.tokens[1].text,
                    firstArgument) &&
                firstArgument == "toggle";
            if (lexed.tokens.size() == 1u || explicitToggle)
            {
                command.verb = UiCommandVerb::Toggle;
            }
            else
            {
                command.verb = UiCommandVerb::Set;
                detail::CopyArguments(lexed.tokens, 1u, command);
            }
            return detail::MakeUiCommandSuccess(std::move(command));
        }
        if (verb == "reload-shaders")
        {
            UiCommand command;
            command.verb = UiCommandVerb::Run;
            command.action.assign(
                UiReloadShadersCommandAction.data(),
                UiReloadShadersCommandAction.size());
            detail::CopyArguments(lexed.tokens, 1u, command);
            return detail::MakeUiCommandSuccess(std::move(command));
        }
        if (verb == "settings.load")
        {
            if (lexed.tokens.size() < 2u)
            {
                return detail::MakeUiCommandError(
                    UiCommandParseError::MissingArgument,
                    input.size(),
                    "settings.load requires exactly one snapshot code.");
            }
            if (lexed.tokens.size() > 2u)
            {
                return detail::MakeUiCommandError(
                    UiCommandParseError::TooManyArguments,
                    lexed.tokens[2].begin,
                    "settings.load accepts exactly one snapshot code.");
            }
            if (lexed.tokens[1].text.empty())
            {
                return detail::MakeUiCommandError(
                    UiCommandParseError::EmptyArgument,
                    lexed.tokens[1].begin,
                    "settings.load snapshot code cannot be empty.");
            }

            UiCommand command;
            command.verb = UiCommandVerb::Run;
            command.action.assign(
                UiSettingsLoadCommandAction.data(),
                UiSettingsLoadCommandAction.size());
            command.arguments.push_back(lexed.tokens[1].text);
            return detail::MakeUiCommandSuccess(std::move(command));
        }

        UiCommand command;
        if (verb == "help")
            command.verb = UiCommandVerb::Help;
        else if (verb == "list")
            command.verb = UiCommandVerb::List;
        else if (verb == "get")
            command.verb = UiCommandVerb::Get;
        else if (verb == "set")
            command.verb = UiCommandVerb::Set;
        else if (verb == "toggle")
            command.verb = UiCommandVerb::Toggle;
        else if (verb == "reset")
            command.verb = UiCommandVerb::Reset;
        else if (verb == "run")
            command.verb = UiCommandVerb::Run;
        else
        {
            return detail::MakeUiCommandError(
                UiCommandParseError::UnknownVerb,
                lexed.tokens.front().begin,
                "Unknown command verb '" +
                    lexed.tokens.front().text + "'.");
        }

        const std::size_t tokenCount = lexed.tokens.size();
        if ((command.verb == UiCommandVerb::Help ||
             command.verb == UiCommandVerb::List) &&
            tokenCount > 2u)
        {
            return detail::MakeUiCommandError(
                UiCommandParseError::TooManyArguments,
                lexed.tokens[2].begin,
                command.verb == UiCommandVerb::Help
                    ? "Help accepts at most one topic."
                    : "List accepts at most one prefix.");
        }
        if (command.verb == UiCommandVerb::Help ||
            command.verb == UiCommandVerb::List)
        {
            detail::CopyArguments(lexed.tokens, 1u, command);
            return detail::MakeUiCommandSuccess(std::move(command));
        }

        const bool exactPathCommand =
            command.verb == UiCommandVerb::Get ||
            command.verb == UiCommandVerb::Toggle ||
            command.verb == UiCommandVerb::Reset;
        if (exactPathCommand && tokenCount < 2u)
        {
            return detail::MakeUiCommandError(
                UiCommandParseError::MissingArgument,
                input.size(),
                std::string(UiCommandVerbName(command.verb)) +
                    " requires exactly one path.");
        }
        if (exactPathCommand && tokenCount > 2u)
        {
            return detail::MakeUiCommandError(
                UiCommandParseError::TooManyArguments,
                lexed.tokens[2].begin,
                std::string(UiCommandVerbName(command.verb)) +
                    " accepts exactly one path.");
        }
        if (exactPathCommand)
        {
            if (lexed.tokens[1].text.empty())
            {
                return detail::MakeUiCommandError(
                    UiCommandParseError::EmptyArgument,
                    lexed.tokens[1].begin,
                    std::string(UiCommandVerbName(command.verb)) +
                        " path cannot be empty.");
            }
            command.path = lexed.tokens[1].text;
            return detail::MakeUiCommandSuccess(std::move(command));
        }

        if (command.verb == UiCommandVerb::Set)
        {
            if (tokenCount < 3u)
            {
                return detail::MakeUiCommandError(
                    UiCommandParseError::MissingArgument,
                    input.size(),
                    "Set requires a path and at least one value.");
            }
            if (lexed.tokens[1].text.empty())
            {
                return detail::MakeUiCommandError(
                    UiCommandParseError::EmptyArgument,
                    lexed.tokens[1].begin,
                    "Set path cannot be empty.");
            }
            command.path = lexed.tokens[1].text;
            detail::CopyArguments(lexed.tokens, 2u, command);
            return detail::MakeUiCommandSuccess(std::move(command));
        }

        if (tokenCount < 2u)
        {
            return detail::MakeUiCommandError(
                UiCommandParseError::MissingArgument,
                input.size(),
                "Run requires an action.");
        }
        if (lexed.tokens[1].text.empty())
        {
            return detail::MakeUiCommandError(
                UiCommandParseError::EmptyArgument,
                lexed.tokens[1].begin,
                "Run action cannot be empty.");
        }
        command.action = lexed.tokens[1].text;
        detail::CopyArguments(lexed.tokens, 2u, command);
        return detail::MakeUiCommandSuccess(std::move(command));
    }

    enum class UiCommandCompletionTarget
    {
        Verb,
        HelpTopic,
        ListPrefix,
        Path,
        Value,
        Action,
        Argument
    };

    struct UiCommandCompletionToken
    {
        UiCommandCompletionTarget target =
            UiCommandCompletionTarget::Verb;
        std::size_t tokenIndex = 0u;
        std::size_t replaceBegin = 0u;
        std::size_t replaceEnd = 0u;
        std::string prefix;
        std::string valuePath;
    };

    [[nodiscard]] inline UiCommandCompletionToken
        GetUiCommandCompletionToken(
            std::string_view input,
            std::size_t cursor)
    {
        cursor = std::min(cursor, input.size());
        const detail::UiCommandLexResult lexed =
            detail::LexUiCommand(input, true);

        UiCommandCompletionToken completion;
        bool foundToken = false;
        for (std::size_t index = 0u;
            index < lexed.tokens.size();
            ++index)
        {
            const detail::UiCommandLexToken& token = lexed.tokens[index];
            if (cursor >= token.begin && cursor <= token.end)
            {
                completion.tokenIndex = index;
                completion.replaceBegin = token.begin;
                completion.replaceEnd = token.end;
                completion.prefix = detail::DecodeCompletionPrefix(
                    input.substr(token.begin, cursor - token.begin));
                foundToken = true;
                break;
            }
        }

        if (!foundToken)
        {
            completion.replaceBegin = cursor;
            completion.replaceEnd = cursor;
            completion.tokenIndex = 0u;
            for (const detail::UiCommandLexToken& token : lexed.tokens)
            {
                if (token.end < cursor)
                    ++completion.tokenIndex;
            }

            if (cursor <= lexed.commandBegin)
            {
                completion.tokenIndex = 0u;
                completion.replaceBegin = lexed.commandBegin;
                completion.replaceEnd = lexed.commandBegin;
            }
        }

        if (completion.tokenIndex == 0u)
            return completion;

        std::string verb;
        if (lexed.tokens.empty() ||
            !detail::NormalizeUiCommandAscii(
                lexed.tokens.front().text,
                verb))
        {
            completion.target = UiCommandCompletionTarget::Argument;
            return completion;
        }

        if (verb == "help")
            completion.target = UiCommandCompletionTarget::HelpTopic;
        else if (verb == "list")
            completion.target = UiCommandCompletionTarget::ListPrefix;
        else if (verb == "get" ||
                 verb == "toggle" ||
                 verb == "reset")
            completion.target = UiCommandCompletionTarget::Path;
        else if (verb == "set")
        {
            completion.target = completion.tokenIndex == 1u
                ? UiCommandCompletionTarget::Path
                : UiCommandCompletionTarget::Value;
            if (completion.target == UiCommandCompletionTarget::Value &&
                lexed.tokens.size() > 1u)
            {
                if (!detail::NormalizeUiCommandAscii(
                        lexed.tokens[1].text,
                        completion.valuePath))
                {
                    completion.valuePath.clear();
                }
            }
        }
        else if (verb == "run")
            completion.target = completion.tokenIndex == 1u
                ? UiCommandCompletionTarget::Action
                : UiCommandCompletionTarget::Argument;
        else if (verb == "skin")
        {
            completion.target = UiCommandCompletionTarget::Value;
            completion.valuePath = "ui.skin";
        }
        else if (verb == "ui")
        {
            completion.target = UiCommandCompletionTarget::Value;
            completion.valuePath = "ui.visible";
        }
        else if (verb == "scene")
        {
            completion.target = UiCommandCompletionTarget::Value;
            completion.valuePath = "scene.current";
        }
        else if (verb == "camera")
        {
            completion.target = UiCommandCompletionTarget::Value;
            completion.valuePath = "camera.mode";
        }
        else
            completion.target = UiCommandCompletionTarget::Argument;

        return completion;
    }

    [[nodiscard]] inline bool UiCommandCompletionMatches(
        std::string_view candidate,
        std::string_view prefix)
    {
        if (!prefix.empty() && prefix.front() == '/')
            prefix.remove_prefix(1u);

        std::string normalizedCandidate;
        std::string normalizedPrefix;
        if (!detail::NormalizeUiCommandAscii(
                candidate,
                normalizedCandidate) ||
            !detail::NormalizeUiCommandAscii(prefix, normalizedPrefix))
        {
            return false;
        }
        return normalizedCandidate.size() >= normalizedPrefix.size() &&
            normalizedCandidate.compare(
                0u,
                normalizedPrefix.size(),
                normalizedPrefix) == 0;
    }

    [[nodiscard]] inline std::vector<std::string_view>
        CompleteUiCommandVerbs(std::string_view prefix)
    {
        std::vector<std::string_view> matches;
        for (const std::string_view candidate :
            UiCommandVerbCompletionCandidates)
        {
            if (UiCommandCompletionMatches(candidate, prefix))
                matches.push_back(candidate);
        }
        return matches;
    }
}
