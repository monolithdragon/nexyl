"""Module-table gate: keeps docs/NEXYL_ARCHITECTURE.md section 4.2 and the build in agreement.

  1. The documented table itself obeys the dependency rules R1, R2, R4, R5 and R6.
     (R3 and R7 concern module contents and are checked by the build system at configure time.)
  2. Every module that exists in the build has a row with the same layer, kind and dependencies.

Modules that are documented but not built yet are fine: the table is also the plan.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

import nexyl_ci
from nexyl_ci import Module, Violation

TABLE_DOCUMENT = "docs/NEXYL_ARCHITECTURE.md"

_ROW = re.compile(r"^\| `([^`]+)` \| (\d+) \| (\w+) \| (.*) \|\s*$")
_NAME = re.compile(r"`([^`]+)`")


@dataclass(frozen=True)
class Row:
    name: str
    layer: int
    kind: str
    dependencies: tuple[str, ...]
    line: int


def parse_table(text: str) -> tuple[dict[str, Row], list[tuple[int, str]]]:
    """Returns the rows by module name and a list of (line, name) for duplicated rows."""
    rows: dict[str, Row] = {}
    duplicates: list[tuple[int, str]] = []
    for number, line in enumerate(text.split("\n"), start=1):
        match = _ROW.match(line)
        if not match:
            continue
        name = match.group(1)
        if name in rows:
            duplicates.append((number, name))
            continue
        rows[name] = Row(name, int(match.group(2)), match.group(3), tuple(_NAME.findall(match.group(4))), number)
    return rows, duplicates


def validate_rules(rows: dict[str, Row]) -> list[tuple[int, str, str]]:
    """Checks the documented graph. Returns (line, rule, message) tuples."""
    problems: list[tuple[int, str, str]] = []
    for row in rows.values():
        for name in row.dependencies:
            dependency = rows.get(name)
            if dependency is None:
                problems.append((row.line, "DEPENDS", f"'{row.name}' depends on '{name}', which has no row."))
                continue
            if dependency.layer >= row.layer:
                problems.append(
                    (
                        row.line,
                        "R1",
                        f"'{row.name}' (layer {row.layer}) depends on '{name}' (layer {dependency.layer}).",
                    )
                )
            if dependency.kind in ("Impl", "Adapter"):
                is_companion = row.kind == "Editor" and row.name == f"{name}.Editor"
                if row.kind not in ("App", "Tool") and not is_companion:
                    problems.append(
                        (row.line, "R2", f"'{row.name}' ({row.kind}) depends on leaf '{name}' ({dependency.kind}).")
                    )
            if dependency.kind == "ToolLib" and row.kind not in ("ToolLib", "Editor", "Tool", "App"):
                problems.append((row.line, "R4", f"'{row.name}' ({row.kind}) depends on tool library '{name}'."))
            if dependency.kind == "Editor" and row.kind not in ("Editor", "Tool", "App"):
                problems.append((row.line, "R5", f"'{row.name}' ({row.kind}) depends on editor module '{name}'."))
            if dependency.kind == "TestLib":
                problems.append((row.line, "R6", f"'{row.name}' depends on test library '{name}'."))
    return problems


def compare(rows: dict[str, Row], modules: dict[str, Module]) -> list[tuple[int, str, str]]:
    """Checks that every built module matches its documented row."""
    problems: list[tuple[int, str, str]] = []
    for module in modules.values():
        row = rows.get(module.name)
        if row is None:
            problems.append((1, "TABLE", f"Module '{module.name}' is built but has no row in the module table."))
            continue
        if row.layer != module.layer:
            problems.append(
                (row.line, "TABLE", f"'{module.name}': documented layer {row.layer}, manifest says {module.layer}.")
            )
        if row.kind != module.kind:
            problems.append(
                (row.line, "TABLE", f"'{module.name}': documented kind {row.kind}, manifest says {module.kind}.")
            )
        documented, declared = set(row.dependencies), set(module.dependencies)
        if documented != declared:
            only_documented = ", ".join(sorted(documented - declared)) or "none"
            only_declared = ", ".join(sorted(declared - documented)) or "none"
            problems.append(
                (
                    row.line,
                    "TABLE",
                    f"'{module.name}': dependencies differ. Only in the table: {only_documented}. "
                    f"Only in the manifest: {only_declared}.",
                )
            )
    return problems


def check(text: str, modules: dict[str, Module]) -> tuple[list[tuple[int, str, str]], int]:
    rows, duplicates = parse_table(text)
    problems = [(line, "TABLE", f"Module '{name}' has more than one row.") for line, name in duplicates]
    if not rows:
        problems.append((1, "TABLE", "The module table was not found."))
    problems += validate_rules(rows)
    problems += compare(rows, modules)
    return problems, len(rows)


def main() -> int:
    arguments = nexyl_ci.argument_parser(__doc__.splitlines()[0], needs_graph=True).parse_args()
    modules = nexyl_ci.load_graph(arguments.graph, arguments.repo)
    text = (arguments.repo / TABLE_DOCUMENT).read_text(encoding="utf-8")
    problems, row_count = check(text, modules)
    violations = [Violation(TABLE_DOCUMENT, line, rule, message) for line, rule, message in problems]
    summary = f"{len(modules)} of {row_count} documented module(s) are built and match the table"
    return nexyl_ci.finish("gate.module_table", violations, summary)


if __name__ == "__main__":
    sys.exit(main())
