"""Tests for the CI gate scripts.

A gate that cannot fail protects nothing, so every rule has at least one case that must be rejected
and one that must be accepted.
"""

from __future__ import annotations

import re
import tempfile
import unittest
from pathlib import Path

import check_manifests
import check_module_table
import check_sources
import nexyl_ci
from nexyl_ci import Module

REPOSITORY = Path(__file__).resolve().parents[3]
PRAGMA = "#pragma once\n"


class Repository:
    """A throwaway source tree with a hand-built module graph."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.modules: dict[str, Module] = {}

    def add_module(
        self,
        name: str,
        kind: str,
        layer: int,
        dependencies: tuple[str, ...] = (),
        group: str = "Runtime",
        files: dict[str, str] | None = None,
        manifest: str | None = None,
    ) -> Module:
        directory = self.root / "Source" / group / name
        directory.mkdir(parents=True)
        default_manifest = f"nexyl_add_module(\n    NAME  {name}\n    KIND  {kind}\n    LAYER {layer})\n"
        (directory / "CMakeLists.txt").write_text(manifest or default_manifest, encoding="utf-8")
        for relative, content in (files or {}).items():
            path = directory / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        module = Module(name, kind, layer, tuple(dependencies), (), directory.resolve())
        self.modules[name] = module
        return module

    def source_rules(self) -> list[str]:
        violations, _ = check_sources.scan_modules(self.modules, self.root)
        return sorted(violation.rule for violation in violations)

    def manifest_rules(self) -> list[str]:
        return sorted(violation.rule for violation in check_manifests.check(self.modules, self.root))


class GateTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self._directory = tempfile.TemporaryDirectory()
        self.addCleanup(self._directory.cleanup)
        self.repository = Repository(Path(self._directory.name).resolve())

    def add_base(self) -> Module:
        return self.repository.add_module(
            "Base",
            "Foundation",
            0,
            files={
                "Public/Nexyl/Base/Std/CStdInt.h": PRAGMA + "#include <cstdint>\n",
                "Public/Nexyl/Base/Types.h": PRAGMA
                + "#include <Nexyl/Base/Std/CStdInt.h>\nusing u8 = std::uint8_t;\n",
            },
        )


class StripCppTests(unittest.TestCase):
    def code(self, text: str) -> str:
        return nexyl_ci.strip_cpp(text)[1]

    def test_line_comment_is_removed(self) -> None:
        self.assertNotIn("std", self.code("int a; // std::vector\nint b;"))

    def test_block_comment_keeps_line_breaks(self) -> None:
        text = "a /* one\ntwo\nthree */ b"
        without_comments, code = nexyl_ci.strip_cpp(text)
        self.assertEqual(without_comments.count("\n"), 2)
        self.assertEqual(len(code), len(text))
        self.assertNotIn("two", code)

    def test_string_contents_are_blanked_only_in_code(self) -> None:
        without_comments, code = nexyl_ci.strip_cpp('auto s = "std::string"; int y;')
        self.assertIn("std::string", without_comments)
        self.assertNotIn("std", code)
        self.assertIn("int y;", code)

    def test_raw_string_with_embedded_quote(self) -> None:
        code = self.code('auto s = R"x(std::a " b)x"; int y;')
        self.assertNotIn("std", code)
        self.assertIn("int y;", code)

    def test_digit_separator_is_not_a_character_literal(self) -> None:
        self.assertIn("std::b", self.code("int a = 1'000'000; std::b;"))

    def test_character_literal_holding_a_quote(self) -> None:
        self.assertIn("std::c", self.code("char q = '\"'; std::c;"))

    def test_prefixed_character_literal(self) -> None:
        self.assertIn("std::d", self.code("auto w = L'a'; std::d;"))

    def test_comment_marker_inside_string(self) -> None:
        self.assertIn("std::e", self.code('auto url = "http://x"; std::e;'))


class SourceRuleTests(GateTestCase):
    def test_clean_modules_pass(self) -> None:
        self.add_base()
        self.repository.add_module(
            "Scene",
            "Feature",
            10,
            ("Base",),
            files={
                "Public/Nexyl/Scene/Node.h": PRAGMA + "#include <Nexyl/Base/Types.h>\n",
                "Private/Detail/Helper.h": PRAGMA,
                "Private/Node.cpp": '#include <Nexyl/Scene/Node.h>\n\n#include "Detail/Helper.h"\n',
            },
        )
        self.assertEqual(self.repository.source_rules(), [])

    def test_header_without_pragma_once(self) -> None:
        self.repository.add_module("Memory", "Foundation", 2, files={"Public/Nexyl/Memory/Heap.h": "struct Heap {};\n"})
        self.assertEqual(self.repository.source_rules(), ["HDR-1"])

    def test_standard_headers_outside_the_wrapper(self) -> None:
        self.repository.add_module(
            "Memory", "Foundation", 2, files={"Private/Heap.cpp": "#include <vector>\n#include <cstdint>\n"}
        )
        self.assertEqual(self.repository.source_rules(), ["STL-1", "STL-3"])

    def test_wrapper_accepts_only_whitelisted_headers(self) -> None:
        self.repository.add_module(
            "Base", "Foundation", 0, files={"Public/Nexyl/Base/Std/Vector.h": PRAGMA + "#include <vector>\n"}
        )
        self.assertEqual(self.repository.source_rules(), ["STL-3"])

    def test_std_token_outside_base(self) -> None:
        self.repository.add_module(
            "Memory",
            "Foundation",
            2,
            files={"Private/Heap.cpp": 'std::size_t g = 0; // std::in_comment\nconst char* s = "std::in_string";\n'},
        )
        self.assertEqual(self.repository.source_rules(), ["STL-2"])

    def test_undeclared_dependency(self) -> None:
        self.repository.add_module("Memory", "Foundation", 2, files={"Public/Nexyl/Memory/Heap.h": PRAGMA})
        self.repository.add_module("Scene", "Feature", 10, files={"Private/A.cpp": "#include <Nexyl/Memory/Heap.h>\n"})
        self.assertEqual(self.repository.source_rules(), ["HDR-4"])

    def test_declared_dependency(self) -> None:
        self.repository.add_module("Memory", "Foundation", 2, files={"Public/Nexyl/Memory/Heap.h": PRAGMA})
        self.repository.add_module(
            "Scene", "Feature", 10, ("Memory",), files={"Private/A.cpp": "#include <Nexyl/Memory/Heap.h>\n"}
        )
        self.assertEqual(self.repository.source_rules(), [])

    def test_unknown_nexyl_header(self) -> None:
        self.repository.add_module("Scene", "Feature", 10, files={"Private/A.cpp": "#include <Nexyl/Nope/X.h>\n"})
        self.assertEqual(self.repository.source_rules(), ["HDR-4"])

    def test_quoted_includes(self) -> None:
        self.repository.add_module(
            "Scene",
            "Feature",
            10,
            files={
                "Public/Nexyl/Scene/Node.h": PRAGMA + '#include "Other.h"\n',
                "Private/A.cpp": '#include "../Public/Nexyl/Scene/Node.h"\n#include "Missing.h"\n',
            },
        )
        self.assertEqual(self.repository.source_rules(), ["HDR-3", "HDR-3", "HDR-3"])

    def test_generated_file_is_not_included(self) -> None:
        self.repository.add_module("Scene", "Feature", 10, files={"Private/A.cpp": '#include "Scene.reflect.gen.h"\n'})
        self.assertEqual(self.repository.source_rules(), ["HDR-10"])

    def test_global_allocation(self) -> None:
        self.repository.add_module(
            "Scene",
            "Feature",
            10,
            files={"Private/A.cpp": "int* a = new int;\nvoid F() { delete a; }\nvoid* b = malloc(4);\n"},
        )
        self.assertEqual(self.repository.source_rules(), ["OWN-1", "OWN-1", "OWN-1"])

    def test_deleted_functions_and_operators_are_not_allocation(self) -> None:
        self.repository.add_module(
            "Scene",
            "Feature",
            10,
            files={
                "Private/A.h": PRAGMA
                + "struct A {\n    A(const A&) = delete;\n    void* operator new(unsigned long long) = delete;\n"
                + "    void operator delete(void*) = delete;\n};\n"
            },
        )
        self.assertEqual(self.repository.source_rules(), [])

    def test_placement_new_is_not_allocation(self) -> None:
        self.repository.add_module(
            "Scene",
            "Feature",
            10,
            files={
                "Private/A.cpp": "void F(void* p) {\n    ::new (static_cast<void*>(p)) Item(1);\n"
                "    new (p) ::Ns::Item;\n    int* q = new (int);\n}\n"
            },
        )
        self.assertEqual(self.repository.source_rules(), ["OWN-1"])

    def test_memory_module_may_allocate(self) -> None:
        self.repository.add_module("Memory", "Foundation", 2, files={"Private/A.cpp": "int* a = new int;\n"})
        self.assertEqual(self.repository.source_rules(), [])

    def test_platform_conditionals(self) -> None:
        self.repository.add_module(
            "Scene", "Feature", 10, files={"Private/A.cpp": "#if NX_PLATFORM_WINDOWS\n#endif\n#ifdef _WIN32\n#endif\n"}
        )
        self.assertEqual(self.repository.source_rules(), ["PLAT-1", "PLAT-2"])

    def test_platform_module_may_use_nexyl_platform_macros_only(self) -> None:
        self.repository.add_module(
            "Platform",
            "Foundation",
            1,
            files={"Private/A.cpp": "#if NX_PLATFORM_WINDOWS\n#endif\n#if defined(_MSC_VER)\n#endif\n"},
        )
        self.assertEqual(self.repository.source_rules(), ["PLAT-1"])

    def test_base_defines_the_platform_macros(self) -> None:
        self.repository.add_module(
            "Base", "Foundation", 0, files={"Public/Nexyl/Base/Platform.h": PRAGMA + "#ifdef _WIN32\n#endif\n"}
        )
        self.assertEqual(self.repository.source_rules(), [])

    def test_system_headers(self) -> None:
        self.repository.add_module(
            "Platform",
            "Foundation",
            1,
            files={
                "Private/Windows/File.cpp": "#include <windows.h>\n",
                "Private/Bad.cpp": "#include <vector>\n#include <stdio.h>\n",
                "Public/Nexyl/Platform/File.h": PRAGMA + "#include <windows.h>\n",
            },
        )
        self.repository.add_module("Scene", "Feature", 10, files={"Private/A.cpp": "#include <windows.h>\n"})
        self.assertEqual(self.repository.source_rules(), ["STL-3", "STL-3", "STL-3", "STL-3"])

    def test_adapter_private_code_is_exempt(self) -> None:
        self.repository.add_module(
            "Script.Lua",
            "Adapter",
            8,
            group="Plugins",
            files={"Private/A.cpp": "#include <vector>\n#include <lua.h>\nstd::vector<int>* g = new std::vector<int>;\n"},
        )
        self.assertEqual(self.repository.source_rules(), [])

    def test_adapter_public_headers_are_not_exempt(self) -> None:
        self.repository.add_module(
            "Script.Lua",
            "Adapter",
            8,
            group="Plugins",
            files={"Public/Nexyl/Script/Lua.h": PRAGMA + "#include <vector>\nstd::vector<int> F();\n"},
        )
        self.assertEqual(self.repository.source_rules(), ["STL-2", "STL-3"])

    def test_work_markers(self) -> None:
        self.repository.add_module(
            "Scene",
            "Feature",
            10,
            files={"Private/A.cpp": "// TODO: fix this\n// TODO(NX-12): fix this\n// FIXME later\n"},
        )
        self.assertEqual(self.repository.source_rules(), ["DOC-10", "DOC-10"])

    def test_deviation_marker_suppresses_its_rule_only(self) -> None:
        self.repository.add_module(
            "Scene",
            "Feature",
            10,
            files={
                "Private/A.cpp": "std::size_t a = 0; // NX-DEVIATION(STL-2): interop shim\n"
                "// NX-DEVIATION(STL-2): interop shim\nstd::size_t b = 0;\n"
                "std::size_t c = 0; // NX-DEVIATION(OWN-1): wrong rule\n"
            },
        )
        self.assertEqual(self.repository.source_rules(), ["STL-2"])

    def test_malformed_deviation_marker(self) -> None:
        self.repository.add_module(
            "Scene", "Feature", 10, files={"Private/A.cpp": "std::size_t a = 0; // NX-DEVIATION: because\n"}
        )
        self.assertEqual(self.repository.source_rules(), ["DEV-1", "STL-2"])

    def test_test_library_is_available_to_tests_only(self) -> None:
        self.repository.add_module("Test", "TestLib", 4, files={"Public/Nexyl/Test/Test.h": PRAGMA})
        self.repository.add_module(
            "ECS",
            "Service",
            6,
            files={
                "Tests/WorldTests.cpp": "#include <Nexyl/Test/Test.h>\n",
                "Private/World.cpp": "#include <Nexyl/Test/Test.h>\n",
            },
        )
        self.assertEqual(self.repository.source_rules(), ["HDR-4"])


class ManifestRuleTests(GateTestCase):
    def test_valid_module(self) -> None:
        self.add_base()
        self.assertEqual(self.repository.manifest_rules(), [])

    def test_manifest_with_an_extra_command(self) -> None:
        manifest = "nexyl_add_module(NAME Memory KIND Foundation LAYER 2)\ntarget_compile_options(Nexyl.Memory PRIVATE -O3)\n"
        self.repository.add_module("Memory", "Foundation", 2, manifest=manifest)
        self.assertEqual(self.repository.manifest_rules(), ["CFG-5"])

    def test_commented_out_command_is_ignored(self) -> None:
        manifest = "# target_link_libraries(Nexyl.Memory PRIVATE zlib)\nnexyl_add_module(NAME Memory KIND Foundation LAYER 2)\n"
        self.repository.add_module("Memory", "Foundation", 2, manifest=manifest)
        self.assertEqual(self.repository.manifest_rules(), [])

    def test_manifest_without_a_module_declaration(self) -> None:
        self.repository.add_module("Memory", "Foundation", 2, manifest="add_library(Memory STATIC a.cpp)\n")
        self.assertEqual(self.repository.manifest_rules(), ["CFG-5"])

    def test_unexpected_entries(self) -> None:
        self.repository.add_module(
            "Memory",
            "Foundation",
            2,
            files={"Scripts/tool.py": "", "Public/Other/A.h": PRAGMA, "Private/Sub/CMakeLists.txt": ""},
        )
        self.assertEqual(self.repository.manifest_rules(), ["CFG-5", "HDR-11", "HDR-11"])

    def test_directory_that_is_not_a_module(self) -> None:
        self.add_base()
        (self.repository.root / "Source" / "Runtime" / "Scratch").mkdir()
        (self.repository.root / "Source" / "Misc").mkdir()
        self.assertEqual(self.repository.manifest_rules(), ["HDR-11", "HDR-11"])


def table(*rows: str) -> str:
    header = "| Module | Layer | Kind | Direct dependencies |\n|---|---|---|---|\n"
    return header + "\n".join(rows) + "\n"


def module(name: str, kind: str, layer: int, *dependencies: str) -> Module:
    return Module(name, kind, layer, tuple(dependencies), (), Path("."))


class ModuleTableTests(unittest.TestCase):
    BASE = "| `Base` | 0 | Foundation | — |"
    MEMORY = "| `Memory` | 2 | Foundation | `Base` |"

    def rules(self, text: str, modules: dict[str, Module] | None = None) -> list[str]:
        problems, _ = check_module_table.check(text, modules or {})
        return sorted(rule for _, rule, _ in problems)

    def test_valid_table_matching_the_build(self) -> None:
        modules = {"Base": module("Base", "Foundation", 0), "Memory": module("Memory", "Foundation", 2, "Base")}
        self.assertEqual(self.rules(table(self.BASE, self.MEMORY), modules), [])

    def test_same_layer_dependency(self) -> None:
        self.assertEqual(self.rules(table(self.BASE, self.MEMORY, "| `Math` | 2 | Foundation | `Memory` |")), ["R1"])

    def test_leaf_dependency(self) -> None:
        rows = table(self.BASE, "| `RHI.Vulkan` | 8 | Impl | `Base` |", "| `Renderer` | 9 | Feature | `RHI.Vulkan` |")
        self.assertEqual(self.rules(rows), ["R2"])

    def test_editor_companion_and_app_may_use_a_leaf(self) -> None:
        rows = table(
            self.BASE,
            "| `Script.Visual` | 8 | Impl | `Base` |",
            "| `Script.Visual.Editor` | 15 | Editor | `Script.Visual` |",
            "| `App.Player` | 16 | App | `Script.Visual` |",
        )
        self.assertEqual(self.rules(rows), [])

    def test_tool_library_editor_and_test_library_rules(self) -> None:
        rows = table(
            self.BASE,
            "| `Test` | 4 | TestLib | `Base` |",
            "| `ShaderCompiler` | 6 | ToolLib | `Base` |",
            "| `Editor` | 12 | Editor | `Base` |",
            "| `Shader` | 8 | Feature | `ShaderCompiler` |",
            "| `Sim.Core` | 13 | Domain | `Editor` |",
            "| `ECS` | 6 | Service | `Test` |",
        )
        self.assertEqual(self.rules(rows), ["R4", "R5", "R6"])

    def test_dependency_without_a_row(self) -> None:
        self.assertEqual(self.rules(table("| `Memory` | 2 | Foundation | `Base` |")), ["DEPENDS"])

    def test_duplicate_row(self) -> None:
        self.assertEqual(self.rules(table(self.BASE, self.BASE)), ["TABLE"])

    def test_built_module_missing_from_the_table(self) -> None:
        self.assertEqual(self.rules(table(self.BASE), {"Memory": module("Memory", "Foundation", 2)}), ["TABLE"])

    def test_built_module_differs_from_its_row(self) -> None:
        modules = {"Base": module("Base", "Foundation", 0), "Memory": module("Memory", "Service", 3)}
        self.assertEqual(self.rules(table(self.BASE, self.MEMORY), modules), ["TABLE", "TABLE", "TABLE"])

    def test_missing_table(self) -> None:
        self.assertEqual(self.rules("no table here\n"), ["TABLE"])

    def test_the_real_architecture_document_obeys_the_rules(self) -> None:
        text = (REPOSITORY / check_module_table.TABLE_DOCUMENT).read_text(encoding="utf-8")
        rows, duplicates = check_module_table.parse_table(text)
        self.assertGreaterEqual(len(rows), 50)
        self.assertEqual(duplicates, [])
        self.assertEqual(check_module_table.validate_rules(rows), [])


class ToolVersionTests(unittest.TestCase):
    def test_major_version_is_parsed_from_every_vendor_format(self) -> None:
        self.assertEqual(nexyl_ci.parse_llvm_major("clang-format version 23.1.5 (https://example.invalid)"), 23)
        self.assertEqual(nexyl_ci.parse_llvm_major("Ubuntu clang-format version 19.1.1 (1ubuntu1~24.04.2)"), 19)
        self.assertEqual(nexyl_ci.parse_llvm_major("LLVM (http://llvm.org/):\n  LLVM version 23.1.5\n"), 23)
        self.assertIsNone(nexyl_ci.parse_llvm_major("not a version string"))

    def test_ci_workflow_installs_the_pinned_version(self) -> None:
        workflow = (REPOSITORY / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
        match = re.search(r'NEXYL_LLVM_VERSION:\s*"(\d+)"', workflow)
        self.assertIsNotNone(match, "the workflow must set NEXYL_LLVM_VERSION")
        self.assertEqual(int(match.group(1)), nexyl_ci.REQUIRED_LLVM_MAJOR)


if __name__ == "__main__":
    unittest.main()
