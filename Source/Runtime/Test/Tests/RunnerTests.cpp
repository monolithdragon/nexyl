// Tests of the test runner itself. Inner test tables with deliberately failing checks are run quietly from
// inside an outer test, so that the outer run proves how failures are counted.

#include <Nexyl/Base/Types.h>
#include <Nexyl/Test/Runner.h>
#include <Nexyl/Test/Test.h>

namespace {

using namespace Nexyl;
using namespace Nexyl::Test;

bool g_reachedAfterRequire = false;
i32 g_bodiesRun = 0;

void InnerPasses() {
    ++g_bodiesRun;
    NX_CHECK(1 + 1 == 2);
}

void InnerFailsTwice() {
    ++g_bodiesRun;
    NX_CHECK(1 + 1 == 3);
    NX_CHECK(false);
}

void InnerRequireStops() {
    ++g_bodiesRun;
    NX_REQUIRE(false);
    g_reachedAfterRequire = true;
}

constexpr TestCase kInnerTests[] = {
    {"Inner", "Passes", &InnerPasses, "", 0},
    {"Inner", "FailsTwice", &InnerFailsTwice, "", 0},
    {"Inner", "RequireStops", &InnerRequireStops, "", 0},
};

RunOptions Quiet(const char* filter = nullptr) {
    RunOptions options;
    options.filter = filter;
    options.quiet = true;
    return options;
}

} // namespace

NX_TEST(Runner, CountsRunAndFailedTests) {
    g_bodiesRun = 0;
    const RunSummary summary = RunTests(kInnerTests, Quiet());
    NX_CHECK(summary.ran == 3);
    NX_CHECK(summary.failed == 2);
    NX_CHECK(g_bodiesRun == 3);
}

NX_TEST(Runner, RequireReturnsFromTheTest) {
    g_reachedAfterRequire = false;
    const RunSummary summary = RunTests(kInnerTests, Quiet("RequireStops"));
    NX_CHECK(summary.ran == 1 && summary.failed == 1);
    NX_CHECK(!g_reachedAfterRequire);
}

NX_TEST(Runner, FilterMatchesSubstringOfFullName) {
    NX_CHECK(RunTests(kInnerTests, Quiet("Inner.Pass")).ran == 1);
    NX_CHECK(RunTests(kInnerTests, Quiet("Inner.")).ran == 3);
    NX_CHECK(RunTests(kInnerTests, Quiet("Fails")).ran == 1);
    NX_CHECK(RunTests(kInnerTests, Quiet("NoSuchTest")).ran == 0);
}

NX_TEST(Runner, ListOnlyRunsNothing) {
    g_bodiesRun = 0;
    RunOptions options = Quiet();
    options.listOnly = true;
    const RunSummary summary = RunTests(kInnerTests, options);
    NX_CHECK(summary.ran == 0 && g_bodiesRun == 0);
}

NX_TEST(Runner, InnerFailuresDoNotLeakIntoTheOuterTest) {
    // The inner run reports failures. If they were charged to this outer test, the outer run would fail here.
    const RunSummary summary = RunTests(kInnerTests, Quiet("FailsTwice"));
    NX_CHECK(summary.failed == 1);
}

NX_TEST(Runner, MainParsesTheCommandLine) {
    // Only command lines that end before any test runs are used here, so no inner output mixes with this run.
    const char* const noArguments[] = {"tests"};
    NX_CHECK(RunTestsMain(Span<const TestCase>(), 1, noArguments) == 1);

    const char* const invalid[] = {"tests", "--bogus"};
    NX_CHECK(RunTestsMain(kInnerTests, 2, invalid) == 2);

    const char* const missingValue[] = {"tests", "--filter"};
    NX_CHECK(RunTestsMain(kInnerTests, 2, missingValue) == 2);

    const char* const help[] = {"tests", "--help"};
    NX_CHECK(RunTestsMain(kInnerTests, 2, help) == 0);
}
