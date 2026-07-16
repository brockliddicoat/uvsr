#!/usr/bin/env python3
"""Validate Title Case in tracked Markdown headings and bold lead-ins."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


SMALL_WORDS = frozenset(
    {
        "a",
        "an",
        "and",
        "as",
        "at",
        "but",
        "by",
        "for",
        "from",
        "if",
        "in",
        "into",
        "nor",
        "of",
        "off",
        "on",
        "onto",
        "or",
        "out",
        "over",
        "per",
        "so",
        "than",
        "the",
        "to",
        "up",
        "via",
        "with",
        "yet",
    }
)

# Established names, acronyms, and version labels keep their exact spelling.
# Add a token here only when repository usage proves that it is a proper name or
# acronym; broad mixed-case/all-capital exemptions let malformed headings pass.
PRESERVED_WORDS = frozenset(
    {
        "AgX",
        "AO",
        "API",
        "BSDF",
        "CDF",
        "CMake",
        "CPU",
        "D3D12",
        "DirectX",
        "DX11",
        "GGX",
        "GI",
        "GitHub",
        "GLB",
        "GPU",
        "HDRP",
        "HUD",
        "ID",
        "LUT",
        "LUTs",
        "NRA-RTAA",
        "PBR",
        "README",
        "README.md",
        "RTAA",
        "SSRT3",
        "UI",
        "UVSR",
        "XeGTAO",
        "glTF",
        "v1",
        "v2",
    }
)

ATX_RE = re.compile(
    r"^(?P<indent>[ \t]*)(?P<marks>#{1,6})[ \t]+(?P<title>.+?)[ \t]*$"
)
SETEXT_RE = re.compile(r"^(?P<indent>[ \t]*)(?:=+|-+)[ \t]*$")
FENCE_OPEN_RE = re.compile(
    r"^(?P<indent>[ \t]*)(?P<fence>`{3,}|~{3,})(?P<info>.*)$"
)
LIST_ITEM_RE = re.compile(
    r"^(?P<indent>[ \t]*)(?P<list_marker>[-+*]|\d+[.)])\s+"
)
BOLD_LIST_RE = re.compile(
    r"^(?P<indent>[ \t]*)(?P<list_marker>[-+*]|\d+[.)])\s+"
    r"(?:\[[ xX]\]\s+)?(?P<marker>\*\*|__)(?P<rest>.*)$"
)
BOLD_PARAGRAPH_RE = re.compile(
    r"^(?P<indent>[ \t]*)(?P<marker>\*\*|__)(?P<rest>.*)$"
)
BLOCKQUOTE_PREFIX_RE = re.compile(r"^ {0,3}>[ \t]?")
RAW_HTML_CLOSING_OPEN_RE = re.compile(
    r"^[ \t]*<(?P<tag>pre|script|style|textarea)(?:[ \t>])", re.I
)
RAW_HTML_CLOSING_RE = re.compile(
    r"^[ \t]*</(?:pre|script|style|textarea)\s*>", re.I
)
RAW_HTML_BLOCK_TAGS = (
    "address",
    "article",
    "aside",
    "base",
    "basefont",
    "blockquote",
    "body",
    "caption",
    "center",
    "col",
    "colgroup",
    "dd",
    "details",
    "dialog",
    "dir",
    "div",
    "dl",
    "dt",
    "fieldset",
    "figcaption",
    "figure",
    "footer",
    "form",
    "frame",
    "frameset",
    "h1",
    "h2",
    "h3",
    "h4",
    "h5",
    "h6",
    "head",
    "header",
    "hr",
    "html",
    "iframe",
    "legend",
    "li",
    "link",
    "main",
    "menu",
    "menuitem",
    "nav",
    "noframes",
    "ol",
    "optgroup",
    "option",
    "p",
    "param",
    "search",
    "section",
    "summary",
    "table",
    "tbody",
    "td",
    "tfoot",
    "th",
    "thead",
    "title",
    "tr",
    "track",
    "ul",
)
RAW_HTML_BLOCK_OPEN_RE = re.compile(
    r"^[ \t]*</?(?P<tag>" + "|".join(RAW_HTML_BLOCK_TAGS) + r")(?:[ \t/>])",
    re.I,
)
RAW_HTML_PROCESSING_OPEN_RE = re.compile(r"^[ \t]*<\?")
RAW_HTML_DECLARATION_OPEN_RE = re.compile(r"^[ \t]*<![A-Z]")
RAW_HTML_CDATA_OPEN_RE = re.compile(r"^[ \t]*<!\[CDATA\[")
RAW_HTML_TYPE7_OPEN_RE = re.compile(
    r"^[ \t]*</?[A-Za-z][A-Za-z0-9-]*(?:[ \t]+[^<>]*)?/?>[ \t]*$"
)
PROTECTED_SPAN_RE = re.compile(
    r"(?<!`)(?P<code_ticks>`+)(?!`).*?(?<!`)(?P=code_ticks)(?!`)"
    r"|\u201c[^\u201d]*\u201d|\"[^\"]*\"|\{[^{}\r\n]+\}|<[^>\r\n]+>"
    r"|(?<=\]\()[^)\r\n]+(?=\))"
)
WORD_RE = re.compile(
    r"[A-Za-z0-9](?:[A-Za-z0-9._/\\\-'\u2019]*[A-Za-z0-9])?"
)


@dataclass(frozen=True)
class Heading:
    path: Path
    line_number: int
    kind: str
    title: str


def _protect_literal_spans(text: str) -> tuple[str, list[str]]:
    protected: list[str] = []

    def replace(match: re.Match[str]) -> str:
        protected.append(match.group(0))
        return f"PROTECTEDTOKEN{len(protected) - 1}"

    return PROTECTED_SPAN_RE.sub(replace, text), protected


def _restore_literal_spans(text: str, protected: Sequence[str]) -> str:
    def restore(match: re.Match[str]) -> str:
        index = int(match.group(1))
        if index >= len(protected):
            return match.group(0)
        return protected[index]

    return re.sub(r"PROTECTEDTOKEN(\d+)", restore, text)


def _preserve_core(core: str) -> bool:
    if core in PRESERVED_WORDS:
        return True
    if re.fullmatch(r"PROTECTEDTOKEN\d+", core):
        return True
    if "_" in core or "\\" in core:
        return True
    if re.fullmatch(r"\d+(?:\.\d+)+", core):
        return True
    if re.fullmatch(r"v\d+(?:\.\d+)*", core):
        return True
    if re.search(r"\.[A-Za-z][A-Za-z0-9]{0,7}$", core):
        return True
    if "/" in core and any("." in component for component in core.split("/")):
        return True
    return False


def _capitalize_component(component: str) -> str:
    if not component:
        return component
    return component[0].upper() + component[1:].lower()


def _titleize_core(
    core: str, *, is_first: bool, is_last: bool, force_capital: bool
) -> str:
    if _preserve_core(core):
        return core
    possessive = re.fullmatch(r"(?P<stem>.+?)(?P<suffix>['\u2019])[sS]", core)
    if possessive and _preserve_core(possessive.group("stem")):
        return possessive.group("stem") + possessive.group("suffix") + "s"

    components = re.split(r"([-/])", core)
    result: list[str] = []
    word_component_index = 0
    word_component_count = (len(components) + 1) // 2
    for component in components:
        if component in {"-", "/"}:
            result.append(component)
            continue

        if _preserve_core(component):
            result.append(component)
            word_component_index += 1
            continue

        lower = component.lower()
        component_is_first = is_first and word_component_index == 0
        component_is_last = (
            is_last and word_component_index == word_component_count - 1
        )
        compound_first = word_component_count > 1 and word_component_index == 0
        if (
            lower in SMALL_WORDS
            and not force_capital
            and not component_is_first
            and not component_is_last
            and not compound_first
        ):
            result.append(lower)
        else:
            result.append(_capitalize_component(component))
        word_component_index += 1
    return "".join(result)


def conventional_title_case(title: str) -> str:
    """Return the repository's deterministic conventional-English Title Case."""

    masked, protected = _protect_literal_spans(title)
    matches = list(WORD_RE.finditer(masked))
    if not matches:
        return title

    result: list[str] = []
    previous_end = 0
    for index, match in enumerate(matches):
        separator = masked[previous_end : match.start()]
        result.append(separator)
        core = match.group(0)
        result.append(
            _titleize_core(
                core,
                is_first=index == 0,
                is_last=index == len(matches) - 1,
                force_capital=(
                    index == 0
                    or bool(re.search(r"[:\u2013\u2014]\s*$", separator))
                ),
            )
        )
        previous_end = match.end()
    result.append(masked[previous_end:])

    return _restore_literal_spans("".join(result), protected)


def _normalize_heading_text(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def _split_blockquote_prefix(line: str) -> tuple[str, int]:
    content = line
    depth = 0
    while True:
        match = BLOCKQUOTE_PREFIX_RE.match(content)
        if match is None:
            return content, depth
        content = content[match.end() :]
        depth += 1


def _strip_blockquote_prefix(line: str) -> str:
    content, _ = _split_blockquote_prefix(line)
    return content


def _column_width(text: str) -> int:
    width = 0
    for character in text:
        if character == "\t":
            width += 4 - (width % 4)
        else:
            width += 1
    return width


def _indent_width(line: str) -> int:
    prefix = re.match(r"^[ \t]*", line).group(0)
    return _column_width(prefix)


def _parent_list_content_indent(
    lines: Sequence[str], index: int, child_indent: int
) -> int | None:
    for cursor in range(index - 1, -1, -1):
        candidate = _strip_blockquote_prefix(lines[cursor].rstrip("\r\n"))
        if not candidate.strip():
            continue
        candidate_indent = _indent_width(candidate)
        if candidate_indent >= child_indent:
            continue
        list_match = LIST_ITEM_RE.match(candidate)
        if list_match:
            return _column_width(candidate[: list_match.end()])
        if candidate_indent == 0:
            return None
    return None


def _structural_indent_limit(
    lines: Sequence[str], index: int, content: str
) -> int | None:
    indent = _indent_width(content)
    if indent <= 3:
        return 3
    parent_content_indent = _parent_list_content_indent(lines, index, indent)
    if parent_content_indent is None:
        return None
    return parent_content_indent + 3


def _structural_indent_allowed(
    lines: Sequence[str], index: int, content: str
) -> bool:
    limit = _structural_indent_limit(lines, index, content)
    return limit is not None and _indent_width(content) <= limit


def _list_container_content_indent(
    lines: Sequence[str], index: int, content: str
) -> int | None:
    return _parent_list_content_indent(lines, index, _indent_width(content))


def _container_exited(
    content: str,
    blockquote_depth: int,
    opening_blockquote_depth: int,
    list_content_indent: int | None,
) -> bool:
    if opening_blockquote_depth > 0 and blockquote_depth < opening_blockquote_depth:
        return True
    return (
        list_content_indent is not None
        and bool(content.strip())
        and _indent_width(content) < list_content_indent
    )


def _type7_can_start(
    lines: Sequence[str], index: int, blockquote_depth: int
) -> bool:
    """Return whether a type-7 HTML block would not interrupt a paragraph."""

    if index == 0:
        return True
    previous, previous_depth = _split_blockquote_prefix(
        lines[index - 1].rstrip("\r\n")
    )
    if not previous.strip() or previous_depth != blockquote_depth:
        return True
    return bool(
        ATX_RE.match(previous)
        or SETEXT_RE.match(previous)
        or FENCE_OPEN_RE.match(previous)
        or RAW_HTML_CLOSING_OPEN_RE.match(previous)
        or RAW_HTML_CLOSING_RE.match(previous)
        or RAW_HTML_BLOCK_OPEN_RE.match(previous)
        or RAW_HTML_PROCESSING_OPEN_RE.match(previous)
        or RAW_HTML_DECLARATION_OPEN_RE.match(previous)
        or RAW_HTML_CDATA_OPEN_RE.match(previous)
        or RAW_HTML_TYPE7_OPEN_RE.match(previous)
        or previous.lstrip().startswith("<!--")
    )


def _line_starts_interrupting_block(content: str) -> bool:
    return bool(
        ATX_RE.match(content)
        or LIST_ITEM_RE.match(content)
        or FENCE_OPEN_RE.match(content)
        or RAW_HTML_CLOSING_OPEN_RE.match(content)
        or RAW_HTML_BLOCK_OPEN_RE.match(content)
        or RAW_HTML_PROCESSING_OPEN_RE.match(content)
        or RAW_HTML_DECLARATION_OPEN_RE.match(content)
        or RAW_HTML_CDATA_OPEN_RE.match(content)
        or content.lstrip().startswith("<!--")
    )


def _without_html_comment(
    content: str, comment_open: bool
) -> tuple[str, bool]:
    if comment_open:
        closing_index = content.find("-->")
        if closing_index < 0:
            return "", True
        content = content[closing_index + 3 :]
        comment_open = False

    while True:
        opening_index = content.find("<!--")
        if opening_index < 0:
            return content, comment_open
        closing_index = content.find("-->", opening_index + 4)
        if closing_index < 0:
            return content[:opening_index], True
        content = content[:opening_index] + content[closing_index + 3 :]


def extract_headings(path: Path, lines: Sequence[str]) -> list[Heading]:
    headings: list[Heading] = []
    fence_character: str | None = None
    fence_length = 0
    fence_indent_limit = 3
    fence_blockquote_depth = 0
    fence_list_content_indent: int | None = None
    html_comment_open = False
    html_comment_blockquote_depth = 0
    html_comment_list_content_indent: int | None = None
    raw_html_tag: str | None = None
    raw_html_terminator: str | None = None
    raw_html_until_blank = False
    raw_html_blockquote_depth = 0
    raw_html_list_content_indent: int | None = None
    index = 0

    while index < len(lines):
        raw_line = lines[index].rstrip("\r\n")
        content, blockquote_depth = _split_blockquote_prefix(raw_line)

        # Fenced code is opaque. In particular, comment or raw-HTML markers in
        # an example must not leak parser state beyond the closing fence.
        if fence_character is not None:
            if _container_exited(
                content,
                blockquote_depth,
                fence_blockquote_depth,
                fence_list_content_indent,
            ):
                fence_character = None
                fence_length = 0
                fence_indent_limit = 3
                fence_blockquote_depth = 0
                fence_list_content_indent = None
            else:
                closing_fence_re = re.compile(
                    rf"^(?P<indent>[ \t]*){re.escape(fence_character)}"
                    rf"{{{fence_length},}}[ \t]*$"
                )
                closing_fence_match = closing_fence_re.match(content)
                if (
                    closing_fence_match
                    and blockquote_depth == fence_blockquote_depth
                    and _column_width(closing_fence_match.group("indent"))
                    <= fence_indent_limit
                ):
                    fence_character = None
                    fence_length = 0
                    fence_indent_limit = 3
                    fence_blockquote_depth = 0
                    fence_list_content_indent = None
                index += 1
                continue

        if raw_html_tag is not None:
            if _container_exited(
                content,
                blockquote_depth,
                raw_html_blockquote_depth,
                raw_html_list_content_indent,
            ):
                raw_html_tag = None
                raw_html_blockquote_depth = 0
                raw_html_list_content_indent = None
            else:
                if re.search(rf"</{re.escape(raw_html_tag)}\s*>", content, re.I):
                    raw_html_tag = None
                    raw_html_blockquote_depth = 0
                    raw_html_list_content_indent = None
                index += 1
                continue

        if raw_html_terminator is not None:
            if _container_exited(
                content,
                blockquote_depth,
                raw_html_blockquote_depth,
                raw_html_list_content_indent,
            ):
                raw_html_terminator = None
                raw_html_blockquote_depth = 0
                raw_html_list_content_indent = None
            else:
                if raw_html_terminator in content:
                    raw_html_terminator = None
                    raw_html_blockquote_depth = 0
                    raw_html_list_content_indent = None
                index += 1
                continue

        if raw_html_until_blank:
            if _container_exited(
                content,
                blockquote_depth,
                raw_html_blockquote_depth,
                raw_html_list_content_indent,
            ):
                raw_html_until_blank = False
                raw_html_blockquote_depth = 0
                raw_html_list_content_indent = None
            else:
                if not content.strip():
                    raw_html_until_blank = False
                    raw_html_blockquote_depth = 0
                    raw_html_list_content_indent = None
                index += 1
                continue

        if html_comment_open and _container_exited(
            content,
            blockquote_depth,
            html_comment_blockquote_depth,
            html_comment_list_content_indent,
        ):
            html_comment_open = False
            html_comment_blockquote_depth = 0
            html_comment_list_content_indent = None

        comment_line_is_block = (
            html_comment_open or content.lstrip().startswith("<!--")
        )
        comment_was_open = html_comment_open
        content, comment_remains_open = _without_html_comment(
            content, html_comment_open
        )
        # An inline comment marker belongs to the inline content of its own
        # block; even when malformed, it must not open a block comment that
        # swallows later headings.
        html_comment_open = (
            comment_remains_open if comment_line_is_block else False
        )
        if html_comment_open and not comment_was_open:
            html_comment_blockquote_depth = blockquote_depth
            html_comment_list_content_indent = _list_container_content_indent(
                lines, index, content
            )
        elif not html_comment_open:
            html_comment_blockquote_depth = 0
            html_comment_list_content_indent = None
        if comment_line_is_block:
            index += 1
            continue

        raw_html_match = RAW_HTML_CLOSING_OPEN_RE.match(content)
        if raw_html_match:
            tag = raw_html_match.group("tag")
            if (
                _structural_indent_allowed(lines, index, content)
                and not re.search(rf"</{re.escape(tag)}\s*>", content, re.I)
            ):
                raw_html_tag = tag
                raw_html_blockquote_depth = blockquote_depth
                raw_html_list_content_indent = _list_container_content_indent(
                    lines, index, content
                )
            index += 1
            continue

        special_raw_html = (
            (RAW_HTML_PROCESSING_OPEN_RE, "?>"),
            (RAW_HTML_CDATA_OPEN_RE, "]]>"),
            (RAW_HTML_DECLARATION_OPEN_RE, ">"),
        )
        special_raw_html_matched = False
        for opening_re, terminator in special_raw_html:
            opening_match = opening_re.match(content)
            if opening_match and _structural_indent_allowed(
                lines, index, content
            ):
                if terminator not in content[opening_match.end() :]:
                    raw_html_terminator = terminator
                    raw_html_blockquote_depth = blockquote_depth
                    raw_html_list_content_indent = _list_container_content_indent(
                        lines, index, content
                    )
                index += 1
                special_raw_html_matched = True
                break
        if special_raw_html_matched:
            continue

        raw_html_block_match = RAW_HTML_BLOCK_OPEN_RE.match(content)
        if raw_html_block_match and _structural_indent_allowed(
            lines, index, content
        ):
            raw_html_until_blank = True
            raw_html_blockquote_depth = blockquote_depth
            raw_html_list_content_indent = _list_container_content_indent(
                lines, index, content
            )
            index += 1
            continue

        raw_html_type7_match = RAW_HTML_TYPE7_OPEN_RE.match(content)
        if (
            raw_html_type7_match
            and _structural_indent_allowed(lines, index, content)
            and _type7_can_start(lines, index, blockquote_depth)
        ):
            raw_html_until_blank = True
            raw_html_blockquote_depth = blockquote_depth
            raw_html_list_content_indent = _list_container_content_indent(
                lines, index, content
            )
            index += 1
            continue

        fence_match = FENCE_OPEN_RE.match(content)
        if fence_match and _structural_indent_allowed(lines, index, content):
            fence = fence_match.group("fence")
            info = fence_match.group("info")
            if fence[0] != "`" or "`" not in info:
                fence_character = fence[0]
                fence_length = len(fence)
                fence_indent_limit = _structural_indent_limit(
                    lines, index, content
                ) or 3
                fence_blockquote_depth = blockquote_depth
                fence_list_content_indent = _list_container_content_indent(
                    lines, index, content
                )
            index += 1
            continue

        atx_match = ATX_RE.match(content)
        if atx_match and _structural_indent_allowed(lines, index, content):
            title = re.sub(r"[ \t]+#+[ \t]*$", "", atx_match.group("title"))
            headings.append(
                Heading(path, index + 1, "Markdown heading", title.strip())
            )
            index += 1
            continue

        next_content = ""
        next_blockquote_depth = blockquote_depth
        if index + 1 < len(lines):
            next_content, next_blockquote_depth = _split_blockquote_prefix(
                lines[index + 1].rstrip("\r\n")
            )
        content_list_context = _list_container_content_indent(
            lines, index, content
        )
        next_list_context = _list_container_content_indent(
            lines, index + 1, next_content
        )
        setext_match = SETEXT_RE.match(next_content)
        if (
            content.strip()
            and not LIST_ITEM_RE.match(content)
            and not content.lstrip().startswith("|")
            and setext_match is not None
            and next_blockquote_depth == blockquote_depth
            and next_list_context == content_list_context
            and _structural_indent_allowed(lines, index, content)
            and _structural_indent_allowed(lines, index + 1, next_content)
        ):
            start_index = index
            while start_index > 0:
                previous, previous_blockquote_depth = _split_blockquote_prefix(
                    lines[start_index - 1].rstrip("\r\n")
                )
                if (
                    not previous.strip()
                    or previous_blockquote_depth != blockquote_depth
                    or FENCE_OPEN_RE.match(previous)
                    or RAW_HTML_CLOSING_OPEN_RE.match(previous)
                    or RAW_HTML_CLOSING_RE.match(previous)
                    or RAW_HTML_BLOCK_OPEN_RE.match(previous)
                    or RAW_HTML_PROCESSING_OPEN_RE.match(previous)
                    or RAW_HTML_DECLARATION_OPEN_RE.match(previous)
                    or RAW_HTML_CDATA_OPEN_RE.match(previous)
                    or RAW_HTML_TYPE7_OPEN_RE.match(previous)
                    or "<!--" in previous
                    or "-->" in previous
                ):
                    break
                if ATX_RE.match(previous) or previous.lstrip().startswith("|"):
                    break
                if LIST_ITEM_RE.match(previous):
                    break
                if (
                    _list_container_content_indent(
                        lines, start_index - 1, previous
                    )
                    != content_list_context
                ):
                    break
                if not _structural_indent_allowed(lines, start_index - 1, previous):
                    break
                start_index -= 1

            title = _normalize_heading_text(
                " ".join(
                    _strip_blockquote_prefix(candidate.rstrip("\r\n")).strip()
                    for candidate in lines[start_index : index + 1]
                )
            )
            headings = [
                heading
                for heading in headings
                if not start_index + 1 <= heading.line_number <= index + 1
            ]
            headings.append(
                Heading(path, start_index + 1, "setext heading", title)
            )
            index += 2
            continue

        bold_match = BOLD_LIST_RE.match(content)
        is_list_lead = bold_match is not None
        bold_list_context = _list_container_content_indent(
            lines, index, content
        )
        if bold_match and not _structural_indent_allowed(lines, index, content):
            bold_match = None
            is_list_lead = False
        if bold_match is None:
            bold_match = BOLD_PARAGRAPH_RE.match(content)
            if bold_match and not _structural_indent_allowed(
                lines, index, content
            ):
                bold_match = None
        if bold_match:
            marker = bold_match.group("marker")
            remainder = bold_match.group("rest")
            title_parts: list[str] = []
            closing_index = remainder.find(marker)
            if closing_index >= 0:
                title_parts.append(remainder[:closing_index])
                trailing = remainder[closing_index + len(marker) :].strip()
                following_content = ""
                following_blockquote_depth = blockquote_depth
                if index + 1 < len(lines):
                    (
                        following_content,
                        following_blockquote_depth,
                    ) = _split_blockquote_prefix(
                        lines[index + 1].rstrip("\r\n")
                    )
                following_list_context = _list_container_content_indent(
                    lines, index + 1, following_content
                )
                paragraph_ends = (
                    index + 1 >= len(lines)
                    or not following_content.strip()
                    or following_blockquote_depth != blockquote_depth
                    or following_list_context != bold_list_context
                    or _line_starts_interrupting_block(following_content)
                )
                if is_list_lead or (trailing in {"", ":"} and paragraph_ends):
                    headings.append(
                        Heading(
                            path,
                            index + 1,
                            "bold lead-in",
                            _normalize_heading_text(" ".join(title_parts)),
                        )
                    )
                index += 1
                continue

            title_parts.append(remainder)
            continuation_index = index + 1
            found_close = False
            while continuation_index < len(lines):
                (
                    continuation,
                    continuation_blockquote_depth,
                ) = _split_blockquote_prefix(
                    lines[continuation_index].rstrip("\r\n")
                )
                if (
                    not continuation.strip()
                    or continuation_blockquote_depth != blockquote_depth
                    or FENCE_OPEN_RE.match(continuation)
                    or RAW_HTML_CLOSING_OPEN_RE.match(continuation)
                    or RAW_HTML_CLOSING_RE.match(continuation)
                    or RAW_HTML_BLOCK_OPEN_RE.match(continuation)
                    or "<!--" in continuation
                    or "-->" in continuation
                ):
                    break
                closing_index = continuation.find(marker)
                if closing_index >= 0:
                    title_parts.append(continuation[:closing_index])
                    trailing = continuation[closing_index + len(marker) :].strip()
                    found_close = True
                    break
                title_parts.append(continuation)
                continuation_index += 1
            if found_close:
                following_content = ""
                following_blockquote_depth = blockquote_depth
                if continuation_index + 1 < len(lines):
                    (
                        following_content,
                        following_blockquote_depth,
                    ) = _split_blockquote_prefix(
                        lines[continuation_index + 1].rstrip("\r\n")
                    )
                following_list_context = _list_container_content_indent(
                    lines, continuation_index + 1, following_content
                )
                paragraph_ends = (
                    continuation_index + 1 >= len(lines)
                    or not following_content.strip()
                    or following_blockquote_depth != blockquote_depth
                    or following_list_context != bold_list_context
                    or _line_starts_interrupting_block(following_content)
                )
                if is_list_lead or (trailing in {"", ":"} and paragraph_ends):
                    headings.append(
                        Heading(
                            path,
                            index + 1,
                            "bold lead-in",
                            _normalize_heading_text(" ".join(title_parts)),
                        )
                    )
                index = continuation_index + 1
                continue

        index += 1

    return headings


def _repository_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )
    return Path(result.stdout.strip()).resolve()


def _repository_markdown_files(root: Path) -> list[Path]:
    result = subprocess.run(
        [
            "git",
            "-C",
            str(root),
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    relative_paths = [
        Path(raw.decode("utf-8")) for raw in result.stdout.split(b"\0") if raw
    ]
    return [
        root / path
        for path in relative_paths
        if path.suffix.lower() in {".md", ".markdown"}
    ]


def _display_path(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root).as_posix()
    except ValueError:
        return str(path.resolve())


def validate(paths: Iterable[Path], root: Path) -> tuple[int, int]:
    heading_count = 0
    violations: list[tuple[Heading, str]] = []
    for path in paths:
        lines = path.read_text(encoding="utf-8-sig").splitlines(keepends=True)
        for heading in extract_headings(path, lines):
            heading_count += 1
            expected = conventional_title_case(heading.title)
            if heading.title != expected:
                violations.append((heading, expected))

    for heading, expected in violations:
        print(
            f"{_display_path(heading.path, root)}:{heading.line_number}: "
            f"{heading.kind} is not in Title Case",
            file=sys.stderr,
        )
        print(f"  found:    {heading.title}", file=sys.stderr)
        print(f"  expected: {expected}", file=sys.stderr)

    print(
        f"Checked {heading_count} Markdown headings and bold lead-ins; "
        f"found {len(violations)} violation(s)."
    )
    return heading_count, len(violations)


def run_self_tests() -> None:
    cases = {
        "Why the image shimmered": "Why the Image Shimmered",
        "Scope and invariants": "Scope and Invariants",
        "G-buffer layout": "G-Buffer Layout",
        "glTF material import": "glTF Material Import",
        "Native-Resolution Analytical/Reconstructive Temporal Anti-Aliasing v1 Postmortem": (
            "Native-Resolution Analytical/Reconstructive Temporal Anti-Aliasing v1 Postmortem"
        ),
        "Included film-look LUTs": "Included Film-Look LUTs",
        "Bilateral-Grid Local Tone Mapping \u2014 active development": (
            "Bilateral-Grid Local Tone Mapping \u2014 Active Development"
        ),
        "The current sample footprint moved while history acceptance changed "
        "discontinuously.": (
            "The Current Sample Footprint Moved While History Acceptance Changed "
            "Discontinuously."
        ),
        "Use `README.md` and \"literal UI text\" safely": (
            "Use `README.md` and \"literal UI text\" Safely"
        ),
        "from here to there": "From Here to There",
        "Results: and next": "Results: And Next",
        "Results:and next": "Results:And Next",
        "research&development report": "Research&Development Report",
        "What's New": "What's New",
        "Don't Break History": "Don't Break History",
        "Renderer's Guide": "Renderer's Guide",
        "GitHub's API Guide": "GitHub's API Guide",
        "Use ``literal lower`` Safely": "Use ``literal lower`` Safely",
        "Use ``lower content``` safely": "Use ``Lower Content``` Safely",
        "DirectX and AgX UI": "DirectX and AgX UI",
        "What was over-engineered too early": "What Was Over-Engineered Too Early",
        "{literal placeholder} and [API docs](https://example.test/path)": (
            "{literal placeholder} and [API Docs](https://example.test/path)"
        ),
        "THIS IS NOT TITLE CASE": "This Is Not Title Case",
        "before/after results": "Before/After Results",
        "phase2 results": "Phase2 Results",
        "tHiS is wrong": "This Is Wrong",
        "HEADER RULES": "Header Rules",
    }
    many_literals = " and ".join(f"`literal{index}`" for index in range(11))
    cases[many_literals] = many_literals
    for source, expected in cases.items():
        actual = conventional_title_case(source)
        if actual != expected:
            raise AssertionError(
                f"Title Case mismatch for {source!r}: {actual!r} != {expected!r}"
            )

    sample = [
        "# Why the image shimmered\n",
        "\n",
        "1. **The current sample footprint moved while history acceptance changed\n",
        "   discontinuously.** Body text.\n",
        "\n",
        "**Entire bold paragraph**\n",
        "\n",
        "**Inline lead-in** followed by ordinary paragraph text.\n",
        "\n",
        "- **Single list lead:** body text.\n",
        "\n",
        "> ## quoted lower heading\n",
        "\n",
        "- Parent item.\n",
        "    - **nested lower lead:** body text.\n",
        "\n",
        "Root paragraph boundary.\n",
        "\n",
        "    - **ignored indented list code**\n",
        "\n",
        "| **ignored table heading** |\n",
        "\n",
        "    ## ignored indented heading\n",
        "\n",
        "```markdown\n",
        "## ignored code heading\n",
        "```\n",
        "\n",
        "Multi-line setext\n",
        "heading example\n",
        "--------------\n",
        "\n",
        "<!--\n",
        "## ignored comment heading\n",
        "-->\n",
        "\n",
        "<pre>\n",
        "## ignored raw HTML heading\n",
        "</pre>\n",
        "\n",
        "```markdown\n",
        "<!-- unclosed comment marker inside the fence\n",
        "<pre>\n",
        "## ignored fenced-state heading\n",
        "```\n",
        "## after fenced markers\n",
        "\n",
        "```markdown\n",
        "    ```\n",
        "## ignored after over-indented fence closer\n",
        "```\n",
        "## after over-indented fence\n",
        "\n",
        "```markdown\n",
        "> ```\n",
        "## ignored after blockquote-looking fence closer\n",
        "```\n",
        "## after blockquote-looking fence closer\n",
        "\n",
        "- Parent for indented code.\n",
        "      ## ignored list code heading\n",
        "      - **ignored list code lead:**\n",
        "\n",
        "- Parent for a nested fence.\n",
        "    ```markdown\n",
        "    ## ignored nested fenced heading\n",
        "    ```\n",
        "\n",
        "<textarea>\n",
        "## ignored textarea heading\n",
        "</textarea>\n",
        "after textarea\n",
        "----------------\n",
        "\n",
        "<div>\n",
        "## ignored div heading\n",
        "</div>\n",
        "\n",
        "## after div block\n",
        "\n",
        "<?processing instruction\n",
        "## ignored processing-instruction heading\n",
        "?>\n",
        "## after processing instruction\n",
        "\n",
        "<![CDATA[\n",
        "## ignored CDATA heading\n",
        "]]>\n",
        "## after CDATA\n",
        "\n",
        "<custom-tag>\n",
        "## ignored custom-tag heading\n",
        "</custom-tag>\n",
        "\n",
        "## after custom-tag block\n",
        "\n",
        "<!-- note --> ## ignored same-line comment suffix\n",
        "## after same-line comment\n",
        "\n",
        "- Parent for an unclosed list fence.\n",
        "    ```markdown\n",
        "    ## ignored unclosed list-fence heading\n",
        "## after unclosed list fence\n",
        "\n",
        "- Parent for an unclosed list HTML block.\n",
        "    <pre>\n",
        "    ## ignored unclosed list HTML heading\n",
        "## after unclosed list HTML\n",
        "\n",
        "- Parent for an unclosed list comment.\n",
        "    <!--\n",
        "    ## ignored unclosed list comment heading\n",
        "## after unclosed list comment\n",
        "\n",
        "> quoted paragraph\n",
        "-----\n",
        "root paragraph\n",
        "> -----\n",
        "## after mismatched setext containers\n",
        "\n",
        "Paragraph text\n",
        "<custom-tag>\n",
        "## after paragraph-interrupting custom tag\n",
        "\n",
        "> **blockquote bold heading**\n",
        "root paragraph after bold heading\n",
        "\n",
        "**bold heading before ATX**\n",
        "## after bold heading\n",
        "\n",
        "## first heading <!--\n",
        "## after inline unclosed comment\n",
        "\n",
        "- Parent for a mismatched list setext boundary.\n",
        "  lower list title\n",
        "---\n",
        "## after mismatched list setext boundary\n",
        "\n",
        "- Parent for a bold-only list paragraph.\n",
        "  **bold heading inside list**\n",
        "root paragraph after list bold heading\n",
        "\n",
        "- list-item paragraph\n",
        "  <custom-tag>\n",
        "  ## after noninterrupting list custom tag\n",
        "\n",
        "```markdown\n",
        "## ignored open-fence heading\n",
        "``` trailing text\n",
        "## still ignored because the fence is open\n",
    ]
    extracted = extract_headings(Path("self-test.md"), sample)
    expected_titles = [
        "Why the image shimmered",
        "The current sample footprint moved while history acceptance changed "
        "discontinuously.",
        "Entire bold paragraph",
        "Single list lead:",
        "quoted lower heading",
        "nested lower lead:",
        "Multi-line setext heading example",
        "after fenced markers",
        "after over-indented fence",
        "after blockquote-looking fence closer",
        "after textarea",
        "after div block",
        "after processing instruction",
        "after CDATA",
        "after custom-tag block",
        "after same-line comment",
        "after unclosed list fence",
        "after unclosed list HTML",
        "after unclosed list comment",
        "after mismatched setext containers",
        "after paragraph-interrupting custom tag",
        "blockquote bold heading",
        "bold heading before ATX",
        "after bold heading",
        "first heading",
        "after inline unclosed comment",
        "after mismatched list setext boundary",
        "bold heading inside list",
        "after noninterrupting list custom tag",
    ]
    actual_titles = [heading.title for heading in extracted]
    if actual_titles != expected_titles:
        raise AssertionError(f"Parser mismatch: {actual_titles!r} != {expected_titles!r}")

    print(f"Self-test passed ({len(cases)} casing cases and one parser fixture).")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help=(
            "Markdown files to check; defaults to every tracked and nonignored "
            "new Markdown file."
        ),
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run deterministic casing and parser tests instead of scanning files.",
    )
    args = parser.parse_args(argv)

    if args.self_test:
        run_self_tests()
        return 0

    try:
        root = _repository_root()
        paths = [path.resolve() for path in args.paths]
        if not paths:
            paths = _repository_markdown_files(root)
        _, violation_count = validate(paths, root)
        return 1 if violation_count else 0
    except (OSError, subprocess.CalledProcessError, UnicodeError) as error:
        print(f"document Title Case check failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
