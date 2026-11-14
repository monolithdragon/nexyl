"""Source-rule gate: scans every module's sources for violations of docs/CODING_STANDARDS.md.

Implemented rules:
  HDR-1   headers use #pragma once
  HDR-3   public headers are included as <Nexyl/...>; private headers with quotes, relative to Private/
  HDR-4   only declared dependencies are included; never another module's private headers
  HDR-10  generated files are not included by hand-written code
  STL-1   whitelisted standard headers are included only from Base/Public/Nexyl/Base/Std/
  STL-2   the token std:: does not appear outside Base
  STL-3   no other standard, C runtime, system or third-party header
  OWN-1   no global new, delete, malloc or free
  PLAT-1  platform, compiler and architecture are detected only through the NX_ macros
  PLAT-2  conditional compilation on those macros only in platform-facing modules
  DOC-10  pending work is marked TODO(<issue-id>): <text>
  FMT-2   source files are UTF-8
  DEV-1   deviation markers are well formed: // NX-DEVIATION(<rule-id>): <justification>

A well-formed deviation marker on the same line or on the line above suppresses that rule there.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

import nexyl_ci
from nexyl_ci import Module, Violation

STD_WHITELIST = frozenset(
    {
        "cstddef",
        "cstdint",
        "new",
        "initializer_list",
        "compare",
        "type_traits",
        "concepts",
        "coroutine",
        "source_location",
        "atomic",
        "bit",
        "limits",
    }
)
STD_WRAPPER_DIRECTORY = ("Public", "Nexyl", "Base", "Std")

# C runtime headers that have a Nexyl replacement and are therefore banned even where system headers are allowed.
C_RUNTIME_HEADERS = frozenset(
    {"stdio.h", "stdlib.h", "string.h", "math.h", "assert.h", "ctype.h", "locale.h", "setjmp.h"}
)

# Modules whose job is low-level memory: they implement what OWN-1 forbids everyone else to use.
ALLOCATION_MODULES = frozenset({"Base", "Memory", "Containers"})

RAW_PLATFORM_MACROS = frozenset(
    {
        "_WIN32",
        "_WIN64",
        "__linux__",
        "__unix__",
        "__APPLE__",
        "__ANDROID__",
        "__ORBIS__",
        "__PROSPERO__",
        "_GAMING_XBOX",
        "_MSC_VER",
        "__clang__",
        "__GNUC__",
        "__MINGW32__",
        "__x86_64__",
        "__aarch64__",
        "_M_X64",
        "_M_ARM64",
    }
)

_INCLUDE = re.compile(r'^\s*#\s*include\s*(?:<([^>\n]+)>|"([^"\n]+)")')
_INCLUDE_ANY = re.compile(r"^\s*#\s*include\b")
_CONDITIONAL = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef|elif|elifdef|elifndef)\b(.*)$")
_PREPROCESSOR = re.compile(r"^\s*#")
_IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
_NX_PLATFORM_MACRO = re.compile(r"^NX_(?:PLATFORM|COMPILER|ARCH)_\w+$")
_STD_TOKEN = re.compile(r"\bstd\s*::")
_NEW_DELETE = re.compile(r"\b(new|delete)\b")
_C_ALLOCATION = re.compile(r"\b(malloc|calloc|realloc|free|aligned_alloc|_aligned_malloc|_aligned_free)\s*\(")
_WORK_MARKER = re.compile(r"\b(TODO|FIXME|XXX|HACK)\b")
_TODO_FORM = re.compile(r"TODO\([^()\s]+\): \S")
_DEVIATION = re.compile(r"NX-DEVIATION")
_DEVIATION_FORM = re.compile(r"NX-DEVIATION\(([A-Z]+-\d+)\):\s+\S")


def allows_system_headers(module: Module) -> bool:
    """STL-6: modules whose Private/ directory may include operating-system and SDK headers."""
    if module.name == "Platform" or module.kind == "Adapter":
        return True
    return module.kind == "Impl" and module.name.startswith(("RHI.", "Audio."))


def is_platform_facing(module: Module) -> bool:
    """PLAT-2: modules that may compile conditionally on the NX_PLATFORM_* family of macros."""
    return module.name == "Base" or allows_system_headers(module)


def is_system_header(header: str) -> bool:
    """Operating-system and SDK headers have a file extension; C++ standard headers do not."""
    return "." in PurePosixPath(header).name and header not in C_RUNTIME_HEADERS


def allows_third_party(module: Module, area: str) -> bool:
    """STL-5: adapters and tools may use foreign code, but only inside Private/."""
    return area == "Private" and module.kind in ("Adapter", "Tool")


@dataclass
class SourceFile:
    path: Path
    display: str
    module: Module
    relative: PurePosixPath
    raw_lines: list[str]
    lines: list[str]
    code_lines: list[str]
    deviations: dict[int, set[str]] = field(default_factory=dict)

    @property
    def area(self) -> str:
        return self.relative.parts[0] if self.relative.parts else ""

    def flag(self, violations: list[Violation], line: int, rule: str, message: str) -> None:
        if rule in self.deviations.get(line, ()) or rule in self.deviations.get(line - 1, ()):
            return
        violations.append(Violation(self.display, line, rule, message))


def collect_files(module: Module) -> list[Path]:
    files = [
        path
        for path in module.directory.rglob("*")
        if path.is_file() and path.suffix in nexyl_ci.SOURCE_EXTENSIONS and ".gen." not in path.name
    ]
    return sorted(files)


def build_header_owners(modules: dict[str, Module], repo: Path, violations: list[Violation]) -> dict[str, str]:
    """Maps an include path such as 'Nexyl/Base/Types.h' to the module that provides it."""
    owners: dict[str, str] = {}
    for module in modules.values():
        public = module.directory / "Public"
        if not public.is_dir():
            continue
        for header in sorted(public.rglob("*")):
            if not header.is_file():
                continue
            include_path = header.relative_to(public).as_posix()
            if include_path in owners and owners[include_path] != module.name:
                violations.append(
                    Violation(
                        nexyl_ci.relative_path(header, repo),
                        1,
                        "HDR-4",
                        f"<{include_path}> is provided by both '{owners[include_path]}' and '{module.name}'.",
                    )
                )
            owners[include_path] = module.name
    return owners


def _check_pragma_once(source: SourceFile, violations: list[Violation]) -> None:
    if source.path.suffix != ".h":
        return
    for number, line in enumerate(source.lines, start=1):
        if line.strip():
            if line.strip() != "#pragma once":
                source.flag(violations, number, "HDR-1", "A header must start with '#pragma once'.")
            return
    source.flag(violations, 1, "HDR-1", "A header must start with '#pragma once'.")


def _check_foreign_include(source: SourceFile, number: int, header: str, violations: list[Violation]) -> None:
    module = source.module
    in_wrapper = module.name == "Base" and source.relative.parts[:4] == STD_WRAPPER_DIRECTORY

    if in_wrapper:
        if header not in STD_WHITELIST:
            source.flag(violations, number, "STL-3", f"<{header}> is not on the standard-header whitelist.")
        return
    if allows_third_party(module, source.area):
        return
    if source.area == "Private" and allows_system_headers(module) and is_system_header(header):
        return
    if header in STD_WHITELIST:
        source.flag(
            violations,
            number,
            "STL-1",
            f"<{header}> may be included only from Base/Public/Nexyl/Base/Std/. Include the Nexyl wrapper instead.",
        )
    else:
        source.flag(
            violations,
            number,
            "STL-3",
            f"<{header}> is a standard, system or third-party header, which this file may not include.",
        )


def _check_includes(
    source: SourceFile,
    modules: dict[str, Module],
    owners: dict[str, str],
    test_libraries: frozenset[str],
    violations: list[Violation],
) -> None:
    module = source.module
    allowed = {module.name, *module.dependencies}
    if source.area == "Tests":
        allowed |= test_libraries

    for number, line in enumerate(source.lines, start=1):
        match = _INCLUDE.match(line)
        if not match:
            if _INCLUDE_ANY.match(line):
                source.flag(violations, number, "HDR-3", "Computed includes are not allowed.")
            continue

        angle, quoted = match.group(1), match.group(2)
        target = angle if angle is not None else quoted
        if ".gen." in target:
            source.flag(violations, number, "HDR-10", "Generated files are not included by hand-written code.")
            continue

        if angle is not None:
            if not angle.startswith("Nexyl/"):
                _check_foreign_include(source, number, angle, violations)
                continue
            owner = owners.get(angle)
            if owner is None:
                source.flag(violations, number, "HDR-4", f"<{angle}> is not a public header of any module.")
            elif owner not in allowed:
                owner_kind = modules[owner].kind
                source.flag(
                    violations,
                    number,
                    "HDR-4",
                    f"<{angle}> belongs to '{owner}' ({owner_kind}), which is not a declared dependency of '{module.name}'.",
                )
            continue

        # Quoted include: a private header of this module.
        if source.area == "Public":
            source.flag(violations, number, "HDR-3", "A public header may include only <Nexyl/...> headers.")
            continue
        base = module.directory / ("Tests" if source.area == "Tests" else "Private")
        if ".." in PurePosixPath(quoted).parts or not (base / quoted).is_file():
            source.flag(
                violations,
                number,
                "HDR-3",
                f'"{quoted}" must be a path relative to {base.name}/ of this module. Public headers use <Nexyl/...>.',
            )


def _check_std_token(source: SourceFile, violations: list[Violation]) -> None:
    if source.module.name == "Base" or allows_third_party(source.module, source.area):
        return
    for number, line in enumerate(source.code_lines, start=1):
        if _STD_TOKEN.search(line):
            source.flag(violations, number, "STL-2", "'std::' is not used outside Base. Use the Nexyl equivalent.")


def _check_platform_conditionals(source: SourceFile, violations: list[Violation]) -> None:
    module = source.module
    if allows_third_party(module, source.area):
        return
    for number, line in enumerate(source.lines, start=1):
        match = _CONDITIONAL.match(line)
        if not match:
            continue
        for token in _IDENTIFIER.findall(match.group(1)):
            if token in RAW_PLATFORM_MACROS and module.name != "Base":
                source.flag(
                    violations,
                    number,
                    "PLAT-1",
                    f"'{token}' is a raw toolchain macro. Use the NX_PLATFORM_*, NX_COMPILER_* or NX_ARCH_* macros.",
                )
            elif _NX_PLATFORM_MACRO.match(token) and not is_platform_facing(module):
                source.flag(
                    violations,
                    number,
                    "PLAT-2",
                    f"Module '{module.name}' may not compile conditionally on '{token}'. Query a capability at run time.",
                )


def is_placement_new(line: str, end: int) -> bool:
    """True when the `new` that ends at `end` is placement new: `new (address) Type`.

    Placement new constructs an object in memory the caller already owns; it does not allocate. The
    parenthesised-type form `new (Type)` does allocate, and is told apart by what follows the parentheses.
    """
    index = end
    while index < len(line) and line[index] == " ":
        index += 1
    if index >= len(line) or line[index] != "(":
        return False
    depth = 0
    while index < len(line):
        if line[index] == "(":
            depth += 1
        elif line[index] == ")":
            depth -= 1
            if depth == 0:
                break
        index += 1
    rest = line[index + 1 :].lstrip()
    return bool(rest) and (rest[0].isalpha() or rest[0] == "_" or rest.startswith("::"))


def _check_allocation(source: SourceFile, violations: list[Violation]) -> None:
    if source.module.name in ALLOCATION_MODULES or allows_third_party(source.module, source.area):
        return
    for number, line in enumerate(source.code_lines, start=1):
        if _PREPROCESSOR.match(line):
            continue
        for match in _NEW_DELETE.finditer(line):
            before = line[: match.start()].rstrip()
            if before.endswith("operator"):
                continue
            if match.group(1) == "delete" and before.endswith("="):
                continue
            if match.group(1) == "new" and is_placement_new(line, match.end()):
                continue
            source.flag(
                violations,
                number,
                "OWN-1",
                f"Global '{match.group(1)}' is not used. Create objects with MakeUnique, MakeRef or a factory.",
            )
        for match in _C_ALLOCATION.finditer(line):
            source.flag(violations, number, "OWN-1", f"'{match.group(1)}' is not used. Allocate through an IAllocator.")


def _check_work_markers(source: SourceFile, violations: list[Violation]) -> None:
    for number, line in enumerate(source.raw_lines, start=1):
        for match in _WORK_MARKER.finditer(line):
            if match.group(1) == "TODO" and _TODO_FORM.match(line, match.start()):
                continue
            source.flag(violations, number, "DOC-10", "Pending work is marked 'TODO(<issue-id>): <text>'.")


def _read_deviations(source: SourceFile, violations: list[Violation]) -> None:
    for number, line in enumerate(source.raw_lines, start=1):
        if not _DEVIATION.search(line):
            continue
        match = _DEVIATION_FORM.search(line)
        if match:
            source.deviations.setdefault(number, set()).add(match.group(1))
        else:
            violations.append(
                Violation(
                    source.display,
                    number,
                    "DEV-1",
                    "A deviation marker has the form '// NX-DEVIATION(<rule-id>): <justification>'.",
                )
            )


def scan_modules(modules: dict[str, Module], repo: Path) -> tuple[list[Violation], int]:
    """Scans all modules. Returns the violations and the number of files scanned."""
    violations: list[Violation] = []
    owners = build_header_owners(modules, repo, violations)
    test_libraries = frozenset(name for name, module in modules.items() if module.kind == "TestLib")
    file_count = 0

    for module in modules.values():
        for path in collect_files(module):
            file_count += 1
            display = nexyl_ci.relative_path(path, repo)
            try:
                text = path.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                violations.append(Violation(display, 1, "FMT-2", "Source files are UTF-8."))
                continue

            without_comments, code_only = nexyl_ci.strip_cpp(text)
            source = SourceFile(
                path=path,
                display=display,
                module=module,
                relative=PurePosixPath(path.relative_to(module.directory).as_posix()),
                raw_lines=text.split("\n"),
                lines=without_comments.split("\n"),
                code_lines=code_only.split("\n"),
            )
            _read_deviations(source, violations)
            _check_pragma_once(source, violations)
            _check_includes(source, modules, owners, test_libraries, violations)
            _check_std_token(source, violations)
            _check_platform_conditionals(source, violations)
            _check_allocation(source, violations)
            _check_work_markers(source, violations)

    return violations, file_count


def main() -> int:
    arguments = nexyl_ci.argument_parser(__doc__.splitlines()[0], needs_graph=True).parse_args()
    modules = nexyl_ci.load_graph(arguments.graph, arguments.repo)
    violations, file_count = scan_modules(modules, arguments.repo)
    return nexyl_ci.finish("gate.sources", violations, f"{file_count} file(s) in {len(modules)} module(s) scanned")


if __name__ == "__main__":
    sys.exit(main())
