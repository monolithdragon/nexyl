"""Shared helpers for the Nexyl CI gate scripts.

Standard library only, so the gates run wherever Python 3.10+ is available.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

# CTest reports a test that exits with this code as skipped (see cmake/NexylGates.cmake).
SKIP_EXIT_CODE = 77

# The output of clang-format and the findings of clang-tidy depend on the tool version, so the gates
# accept exactly one major version. The CI workflow installs the same one; a self-test keeps them in step.
REQUIRED_LLVM_MAJOR = 23

SOURCE_EXTENSIONS = (".h", ".inl", ".cpp", ".mm")


@dataclass(frozen=True)
class Module:
    """One entry of the module graph exported by cmake/NexylModule.cmake."""

    name: str
    kind: str
    layer: int
    dependencies: tuple[str, ...]
    third_party: tuple[str, ...]
    directory: Path
    reflect: str = "None"
    sim_safe: bool = False


@dataclass(frozen=True, order=True)
class Violation:
    path: str
    line: int
    rule: str
    message: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] {self.message}"


def load_graph(graph_path: Path, repo: Path) -> dict[str, Module]:
    """Loads nexyl_modules.json. Directories become absolute paths."""
    data = json.loads(graph_path.read_text(encoding="utf-8"))
    modules: dict[str, Module] = {}
    for entry in data["modules"]:
        modules[entry["name"]] = Module(
            name=entry["name"],
            kind=entry["kind"],
            layer=int(entry["layer"]),
            dependencies=tuple(entry.get("dependencies", ())),
            third_party=tuple(entry.get("thirdParty", ())),
            directory=(repo / entry["directory"]).resolve(),
            reflect=entry.get("reflect", "None"),
            sim_safe=bool(entry.get("simSafe", False)),
        )
    return modules


def argument_parser(description: str, needs_graph: bool) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--repo", type=Path, required=True, help="repository root")
    if needs_graph:
        parser.add_argument("--graph", type=Path, required=True, help="nexyl_modules.json from the build directory")
    return parser


def relative_path(path: Path, repo: Path) -> str:
    """Repository-relative path with forward slashes, for stable and clickable output."""
    try:
        return path.resolve().relative_to(repo.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def finish(gate: str, violations: list[Violation], summary: str = "") -> int:
    """Prints the result of a gate and returns its process exit code."""
    if violations:
        for violation in sorted(set(violations)):
            print(violation)
        print(f"{gate}: FAILED with {len(set(violations))} violation(s).")
        return 1
    print(f"{gate}: passed{'. ' + summary if summary else '.'}")
    return 0


def find_tool(name: str, environment_variable: str) -> str | None:
    """Locates an external tool: an explicit path in the environment wins over PATH."""
    explicit = os.environ.get(environment_variable)
    if explicit:
        return explicit if Path(explicit).exists() else None
    return shutil.which(name)


def gate_unavailable(gate: str, reason: str) -> int:
    """Exit code for a gate that cannot run here: skipped locally, failed where the tools are required."""
    if os.environ.get("NEXYL_REQUIRE_GATE_TOOLS"):
        print(f"{gate}: FAILED. {reason}, and NEXYL_REQUIRE_GATE_TOOLS is set.", file=sys.stderr)
        return 1
    print(f"{gate}: skipped. {reason}.")
    return SKIP_EXIT_CODE


_LLVM_VERSION = re.compile(r"version (\d+)\.")


def parse_llvm_major(version_output: str) -> int | None:
    """Major version from the --version output of an LLVM tool, whatever the vendor prefix."""
    match = _LLVM_VERSION.search(version_output)
    return int(match.group(1)) if match else None


def find_llvm_tool(gate: str, name: str, environment_variable: str) -> tuple[str | None, int]:
    """Locates clang-format or clang-tidy at the pinned major version.

    Returns (path, 0) when the tool can be used, otherwise (None, exit code for the gate).
    """
    tool = find_tool(name, environment_variable)
    if tool is None:
        return None, gate_unavailable(gate, f"'{name}' was not found on PATH")
    output = subprocess.run([tool, "--version"], capture_output=True, text=True, check=False).stdout
    major = parse_llvm_major(output)
    if major != REQUIRED_LLVM_MAJOR:
        found = "an unknown version" if major is None else f"version {major}"
        return None, gate_unavailable(gate, f"'{name}' is {found}, but version {REQUIRED_LLVM_MAJOR} is required")
    return tool, 0


# ---------------------------------------------------------------------------------------------------------
# C++ lexing: just enough to tell code from comments and literals.
# ---------------------------------------------------------------------------------------------------------

_RAW_STRING_PREFIXES = {"R", "LR", "uR", "UR", "u8R"}


def _blank(segment: str) -> str:
    return "".join("\n" if character == "\n" else " " for character in segment)


def _token_start(text: str, index: int, extra: str) -> int:
    """Start of the identifier-like token that ends just before `index`."""
    start = index
    while start > 0 and (text[start - 1].isalnum() or text[start - 1] in extra):
        start -= 1
    return start


def _is_raw_string(text: str, index: int) -> bool:
    start = _token_start(text, index, "_")
    return text[start:index] in _RAW_STRING_PREFIXES


def _is_digit_separator(text: str, index: int) -> bool:
    """True for the apostrophe in 1'000'000, false for the one that opens a character literal."""
    if index + 1 >= len(text) or not text[index + 1].isalnum():
        return False
    start = _token_start(text, index, "_'.")
    return start < index and text[start].isdigit()


def _scan_quoted(text: str, start: int, quote: str) -> int:
    """Index just past the literal that opens at `start`. An unterminated literal ends at the line break."""
    index = start + 1
    while index < len(text):
        character = text[index]
        if character == "\\":
            index += 2
            continue
        if character == quote:
            return index + 1
        if character == "\n":
            return index
        index += 1
    return len(text)


def _blank_literal(segment: str, quote: str) -> str:
    if len(segment) >= 2 and segment.endswith(quote):
        return segment[0] + _blank(segment[1:-1]) + quote
    return segment[:1] + _blank(segment[1:])


def strip_cpp(text: str) -> tuple[str, str]:
    """Returns (text without comments, text without comments and without literal contents).

    Both results keep every line break and column, so line numbers computed on them match the original.
    """
    without_comments: list[str] = []
    code_only: list[str] = []
    index = 0
    length = len(text)

    while index < length:
        character = text[index]
        following = text[index + 1] if index + 1 < length else ""

        if character == "/" and following == "/":
            end = text.find("\n", index)
            end = length if end == -1 else end
            blanked = _blank(text[index:end])
            without_comments.append(blanked)
            code_only.append(blanked)
            index = end
        elif character == "/" and following == "*":
            end = text.find("*/", index + 2)
            end = length if end == -1 else end + 2
            blanked = _blank(text[index:end])
            without_comments.append(blanked)
            code_only.append(blanked)
            index = end
        elif character == '"':
            end = -1
            if _is_raw_string(text, index):
                opening = text.find("(", index)
                delimiter = text[index + 1 : opening] if opening != -1 else None
                if delimiter is not None and len(delimiter) <= 16 and not any(c in delimiter for c in ' ()\\\n"'):
                    closing = ")" + delimiter + '"'
                    found = text.find(closing, opening)
                    end = length if found == -1 else found + len(closing)
            if end == -1:
                end = _scan_quoted(text, index, '"')
            segment = text[index:end]
            without_comments.append(segment)
            code_only.append(_blank_literal(segment, '"'))
            index = end
        elif character == "'" and not _is_digit_separator(text, index):
            end = _scan_quoted(text, index, "'")
            segment = text[index:end]
            without_comments.append(segment)
            code_only.append(_blank_literal(segment, "'"))
            index = end
        else:
            without_comments.append(character)
            code_only.append(character)
            index += 1

    return "".join(without_comments), "".join(code_only)
