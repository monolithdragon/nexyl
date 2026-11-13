"""Runs a CMake workflow preset (configure, build, test, gates) with the right toolchain environment.

    python Scripts/ci/run_workflow.py win-x64-dev
    python Scripts/ci/run_workflow.py linux-x64-dev --fresh

This is the single entry point used both locally and by CI. On Windows it loads the Visual Studio
developer environment first, which the Ninja generator needs to find the MSVC compiler and linker.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]


def find_visual_studio() -> Path | None:
    """Installation directory of the newest Visual Studio that has the C++ toolset, or None."""
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return None
    query = [
        str(vswhere),
        "-latest",
        "-products",
        "*",
        "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-property",
        "installationPath",
    ]
    for extra in ([], ["-prerelease"]):
        output = subprocess.run(query + extra, capture_output=True, text=True, check=False).stdout.strip()
        if output:
            return Path(output.splitlines()[0])
    return None


def visual_studio_environment() -> dict[str, str] | None:
    """The environment variables set by vcvars64.bat, or None if Visual Studio is not installed."""
    installation = find_visual_studio()
    if installation is None:
        return None
    vcvars = installation / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not vcvars.is_file():
        return None

    # /s with the whole command in an outer pair of quotes is the one quoting form cmd.exe handles reliably.
    command = f'cmd.exe /d /s /c ""{vcvars}" >nul 2>&1 && set"'
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        return None

    environment: dict[str, str] = {}
    for line in result.stdout.splitlines():
        name, separator, value = line.partition("=")
        if separator and name:
            environment[name.upper()] = value
    return environment


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("preset", help="name of a workflow preset from CMakePresets.json")
    parser.add_argument("--fresh", action="store_true", help="discard the existing build directory configuration")
    arguments = parser.parse_args()

    environment = {name.upper(): value for name, value in os.environ.items()} if os.name == "nt" else dict(os.environ)

    if os.name == "nt" and arguments.preset.startswith("win-") and shutil.which("cl") is None:
        visual_studio = visual_studio_environment()
        if visual_studio is not None:
            environment.update(visual_studio)
        elif "clang" not in arguments.preset:
            print(
                "run_workflow: Visual Studio with the C++ toolset was not found, and 'cl' is not on PATH.",
                file=sys.stderr,
            )
            return 1

    command = ["cmake", "--workflow", "--preset", arguments.preset]
    if arguments.fresh:
        command.append("--fresh")
    return subprocess.run(command, cwd=REPOSITORY, env=environment, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
