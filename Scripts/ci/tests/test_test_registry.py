"""Tests for Scripts/build/generate_test_registry.py."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "build"))
import generate_test_registry as registry  # noqa: E402


class RegistryTests(unittest.TestCase):
    def setUp(self) -> None:
        self._directory = tempfile.TemporaryDirectory()
        self.addCleanup(self._directory.cleanup)
        self.root = Path(self._directory.name).resolve()

    def source(self, name: str, text: str) -> Path:
        path = self.root / name
        path.write_text(text, encoding="utf-8")
        return path

    def test_finds_tests_in_order(self) -> None:
        path = self.source("A.cpp", "NX_TEST(Span, Empty) {}\n\nNX_TEST( Span , Slices ) {\n}\n")
        tests = registry.collect([path])
        self.assertEqual([(t.suite, t.name, t.line) for t in tests], [("Span", "Empty", 1), ("Span", "Slices", 3)])
        self.assertEqual(tests[0].function, "NxTestSpanEmpty")

    def test_ignores_comments_and_strings(self) -> None:
        path = self.source("A.cpp", '// NX_TEST(Old, One) {}\n/* NX_TEST(Old, Two) */\nconst char* s = "NX_TEST(Old, Three)";\n')
        self.assertEqual(registry.collect([path]), [])

    def test_files_are_processed_in_sorted_order(self) -> None:
        second = self.source("B.cpp", "NX_TEST(B, One) {}\n")
        first = self.source("A.cpp", "NX_TEST(A, One) {}\n")
        self.assertEqual([t.suite for t in registry.collect([second, first])], ["A", "B"])

    def test_rejects_a_test_inside_conditional_compilation(self) -> None:
        path = self.source("A.cpp", "#if NX_ASSERTS_ENABLED\nNX_TEST(A, Hidden) {}\n#endif\n")
        with self.assertRaisesRegex(registry.RegistryError, "conditional compilation"):
            registry.collect([path])

    def test_accepts_conditional_code_inside_a_test_body(self) -> None:
        path = self.source("A.cpp", "NX_TEST(A, Body) {\n#if NX_ASSERTS_ENABLED\n    int x = 0;\n#endif\n}\nNX_TEST(A, Next) {}\n")
        self.assertEqual(len(registry.collect([path])), 2)

    def test_rejects_duplicate_names(self) -> None:
        path = self.source("A.cpp", "NX_TEST(A, Same) {}\nNX_TEST(A, Same) {}\n")
        with self.assertRaisesRegex(registry.RegistryError, "already defined"):
            registry.collect([path])

    def test_rejects_colliding_function_names(self) -> None:
        path = self.source("A.cpp", "NX_TEST(Ab, C) {}\nNX_TEST(A, bC) {}\n")
        with self.assertRaisesRegex(registry.RegistryError, "same function name"):
            registry.collect([path])

    def test_renders_an_empty_table_without_a_zero_sized_array(self) -> None:
        text = registry.render([], self.root, "Empty")
        self.assertNotIn("kTests[]", text)
        self.assertIn("RunTestsMain(::Nexyl::Span<const ::Nexyl::Test::TestCase>()", text)

    def test_renders_relative_paths(self) -> None:
        path = self.source("A.cpp", "NX_TEST(A, One) {}\n")
        text = registry.render(registry.collect([path]), self.root, "M")
        self.assertIn('{"A", "One", &NxTestAOne, "A.cpp", 1},', text)


if __name__ == "__main__":
    unittest.main()
