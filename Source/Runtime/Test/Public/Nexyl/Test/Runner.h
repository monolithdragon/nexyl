#pragma once

#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Test {

/// The body of a test, as defined by NX_TEST.
/// @since 0.1
using TestFunction = void (*)();

/// One registered test. The build generates the table of these for each module's test executable.
/// @threadsafety safe
/// @since 0.1
struct TestCase {
    /// Suite name, the first argument of NX_TEST.
    const char* suite = "";
    /// Test name, the second argument of NX_TEST.
    const char* name = "";
    /// The test body.
    TestFunction function = nullptr;
    /// Source file of the NX_TEST, relative to the repository root.
    const char* file = "";
    /// Line of the NX_TEST.
    u32 line = 0;
};

/// How RunTests selects and reports tests.
/// @since 0.1
struct RunOptions {
    /// Runs only tests whose full name `Suite.Name` contains this text. Null runs every test.
    const char* filter = nullptr;
    /// Prints the names of the selected tests instead of running them.
    bool listOnly = false;
    /// Suppresses all progress and failure output. Used to test the runner itself.
    bool quiet = false;
};

/// The outcome of RunTests.
/// @since 0.1
struct RunSummary {
    /// Number of tests that ran.
    u32 ran = 0;
    /// Number of tests with at least one failed check.
    u32 failed = 0;
};

/// Runs tests in table order and reports progress and failures on the console.
///
/// While tests run, an assertion handler is installed that reports a failed NX_ASSERT in the code under test
/// with its location and then terminates. Runs may be nested; the outer run is unaffected by an inner one.
///
/// @param tests   The tests to choose from. Borrowed for the duration of the call.
/// @param options Selection and reporting options.
/// @return How many tests ran and how many failed.
/// @threadsafety main
/// @since 0.1
NX_TEST_API RunSummary RunTests(Span<const TestCase> tests, const RunOptions& options);

/// Entry point of a test executable: parses the command line, runs the tests, and returns the exit code.
///
/// Command line: `--filter <text>` selects tests by substring of `Suite.Name`; `--list` prints the selected
/// test names; `--help` prints usage. The exit code is 0 when at least one test ran and none failed,
/// 1 when a test failed or no test was selected, and 2 for an invalid command line.
///
/// @param tests          The generated test table.
/// @param argumentCount  `argc` of main.
/// @param arguments      `argv` of main.
/// @return The process exit code.
/// @threadsafety main
/// @since 0.1
NX_TEST_API int RunTestsMain(Span<const TestCase> tests, int argumentCount, const char* const* arguments);

} // namespace Nexyl::Test
