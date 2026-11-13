"""Static-analysis gate: runs clang-tidy with the repository profile (.clang-tidy) on every engine source.

Uses compile_commands.json from the build directory, so each file is analysed with its real flags.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import nexyl_ci

ANALYSED_EXTENSIONS = (".cpp", ".mm")

# Flags of other compilers that clang-tidy's front end does not know must not count as findings.
EXTRA_ARGUMENTS = ("-Wno-unknown-argument", "-Wno-unused-command-line-argument")


def select_sources(build_directory: Path, repo: Path) -> list[Path]:
    """Engine sources that have a compile command: everything under Source/ except third-party and generated code."""
    commands = json.loads((build_directory / "compile_commands.json").read_text(encoding="utf-8"))
    source_root = (repo / "Source").resolve()
    selected: set[Path] = set()
    for entry in commands:
        path = Path(entry["file"]).resolve()
        if path.suffix not in ANALYSED_EXTENSIONS or ".gen." in path.name:
            continue
        if source_root not in path.parents or "ThirdParty" in path.parts:
            continue
        selected.add(path)
    return sorted(selected)


def analyse(tool: str, build_directory: Path, source: Path) -> tuple[Path, int, str]:
    command = [tool, "-p", str(build_directory), "--quiet"]
    command += [f"--extra-arg={argument}" for argument in EXTRA_ARGUMENTS]
    command.append(str(source))
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    return source, result.returncode, (result.stdout + result.stderr).strip()


def main() -> int:
    parser = nexyl_ci.argument_parser(__doc__.splitlines()[0], needs_graph=False)
    parser.add_argument("--build-dir", type=Path, required=True, help="build directory with compile_commands.json")
    arguments = parser.parse_args()

    tool, unavailable = nexyl_ci.find_llvm_tool("gate.tidy", "clang-tidy", "NEXYL_CLANG_TIDY")
    if tool is None:
        return unavailable
    if not (arguments.build_dir / "compile_commands.json").is_file():
        print(f"gate.tidy: FAILED. No compile_commands.json in {arguments.build_dir}.", file=sys.stderr)
        return 1

    sources = select_sources(arguments.build_dir, arguments.repo)
    failures = 0
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
        for source, code, output in pool.map(lambda path: analyse(tool, arguments.build_dir, path), sources):
            if code != 0:
                failures += 1
                print(f"--- {nexyl_ci.relative_path(source, arguments.repo)}")
                print(output)

    if failures:
        print(f"gate.tidy: FAILED. {failures} of {len(sources)} file(s) have findings.")
        return 1
    print(f"gate.tidy: passed. {len(sources)} file(s) analysed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
