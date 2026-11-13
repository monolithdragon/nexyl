"""Formatting gate: every C++ source matches .clang-format (docs/CODING_STANDARDS.md, section 18).

Run with --fix to reformat the files in place.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import nexyl_ci

SOURCE_ROOTS = ("Source", "Abi", "Tests")
EXTENSIONS = (".h", ".inl", ".cpp", ".mm", ".c")
EXCLUDED_DIRECTORIES = frozenset({"ThirdParty", "build"})
BATCH_SIZE = 64


def find_sources(repo: Path) -> list[Path]:
    sources: list[Path] = []
    for root in SOURCE_ROOTS:
        directory = repo / root
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if not path.is_file() or path.suffix not in EXTENSIONS or ".gen." in path.name:
                continue
            if EXCLUDED_DIRECTORIES.intersection(path.relative_to(repo).parts):
                continue
            sources.append(path)
    return sorted(sources)


def main() -> int:
    parser = nexyl_ci.argument_parser(__doc__.splitlines()[0], needs_graph=False)
    parser.add_argument("--fix", action="store_true", help="reformat files in place instead of checking")
    arguments = parser.parse_args()

    tool, unavailable = nexyl_ci.find_llvm_tool("gate.format", "clang-format", "NEXYL_CLANG_FORMAT")
    if tool is None:
        return unavailable

    version = subprocess.run([tool, "--version"], capture_output=True, text=True, check=False).stdout.strip()
    sources = find_sources(arguments.repo)
    mode = ["-i"] if arguments.fix else ["--dry-run", "--Werror"]

    failed = False
    for start in range(0, len(sources), BATCH_SIZE):
        batch = [str(path) for path in sources[start : start + BATCH_SIZE]]
        result = subprocess.run([tool, "--style=file", *mode, *batch], cwd=arguments.repo, check=False)
        failed = failed or result.returncode != 0

    if failed:
        print(f"gate.format: FAILED ({version}). Run: python Scripts/ci/check_format.py --repo . --fix")
        return 1
    action = "reformatted" if arguments.fix else "checked"
    print(f"gate.format: passed. {len(sources)} file(s) {action} with {version}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
