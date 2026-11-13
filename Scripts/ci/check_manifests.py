"""Manifest and layout gate.

  CFG-5   a module manifest contains exactly one nexyl_add_module() call and no other command,
          so that a module can never set compiler flags or link libraries on its own
  HDR-11  a module directory contains only CMakeLists.txt, Public/, Private/ and Tests/;
          Public/ contains only Nexyl/; every directory of a source group is a module
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

import nexyl_ci
from nexyl_ci import Module, Violation

SOURCE_GROUPS = ("Runtime", "Plugins", "Editor", "Tools", "Apps")
OTHER_SOURCE_DIRECTORIES = ("ThirdParty",)
MODULE_ENTRIES = frozenset({"CMakeLists.txt", "Public", "Private", "Tests"})

_COMMAND = re.compile(r"^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(", re.MULTILINE)


def strip_cmake_comments(text: str) -> str:
    """Removes line comments. A '#' inside a double-quoted argument is kept."""
    result: list[str] = []
    for line in text.split("\n"):
        in_quotes = False
        cut = len(line)
        for index, character in enumerate(line):
            if character == '"' and (index == 0 or line[index - 1] != "\\"):
                in_quotes = not in_quotes
            elif character == "#" and not in_quotes:
                cut = index
                break
        result.append(line[:cut])
    return "\n".join(result)


def manifest_commands(text: str) -> list[str]:
    return _COMMAND.findall(strip_cmake_comments(text))


def check(modules: dict[str, Module], repo: Path) -> list[Violation]:
    violations: list[Violation] = []

    for module in modules.values():
        directory = module.directory
        manifest = directory / "CMakeLists.txt"
        manifest_display = nexyl_ci.relative_path(manifest, repo)

        for entry in sorted(directory.iterdir()):
            if entry.name not in MODULE_ENTRIES:
                violations.append(
                    Violation(
                        nexyl_ci.relative_path(entry, repo),
                        1,
                        "HDR-11",
                        "A module directory contains only CMakeLists.txt, Public/, Private/ and Tests/.",
                    )
                )

        public = directory / "Public"
        if public.is_dir():
            for entry in sorted(public.iterdir()):
                if entry.name != "Nexyl":
                    violations.append(
                        Violation(nexyl_ci.relative_path(entry, repo), 1, "HDR-11", "Public/ contains only Nexyl/.")
                    )

        commands = manifest_commands(manifest.read_text(encoding="utf-8"))
        if commands != ["nexyl_add_module"]:
            listed = ", ".join(commands) if commands else "none"
            violations.append(
                Violation(
                    manifest_display,
                    1,
                    "CFG-5",
                    f"A module manifest contains exactly one nexyl_add_module() call. Found: {listed}.",
                )
            )

        for nested in sorted(directory.rglob("CMakeLists.txt")):
            if nested != manifest:
                violations.append(
                    Violation(
                        nexyl_ci.relative_path(nested, repo), 1, "CFG-5", "A module has exactly one CMakeLists.txt."
                    )
                )

    # Every directory of a source group must be a configured module.
    module_directories = {module.directory for module in modules.values()}
    source = repo / "Source"
    if source.is_dir():
        for entry in sorted(source.iterdir()):
            if entry.is_dir() and entry.name not in SOURCE_GROUPS and entry.name not in OTHER_SOURCE_DIRECTORIES:
                violations.append(
                    Violation(
                        nexyl_ci.relative_path(entry, repo),
                        1,
                        "HDR-11",
                        f"Source/ contains only {', '.join(SOURCE_GROUPS + OTHER_SOURCE_DIRECTORIES)}.",
                    )
                )
        for group in SOURCE_GROUPS:
            group_directory = source / group
            if not group_directory.is_dir():
                continue
            for entry in sorted(group_directory.iterdir()):
                if entry.resolve() not in module_directories:
                    violations.append(
                        Violation(
                            nexyl_ci.relative_path(entry, repo),
                            1,
                            "HDR-11",
                            f"Everything in Source/{group}/ is a module directory with a manifest.",
                        )
                    )

    return violations


def main() -> int:
    arguments = nexyl_ci.argument_parser(__doc__.splitlines()[0], needs_graph=True).parse_args()
    modules = nexyl_ci.load_graph(arguments.graph, arguments.repo)
    return nexyl_ci.finish("gate.manifests", check(modules, arguments.repo), f"{len(modules)} manifest(s) checked")


if __name__ == "__main__":
    sys.exit(main())
