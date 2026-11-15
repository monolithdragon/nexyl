#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/LogHook.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

namespace {

AssertInfo g_lastAssert;
i32 g_assertCount = 0;

AssertAction RecordAssert(const AssertInfo& info) {
    g_lastAssert = info;
    ++g_assertCount;
    return AssertAction::Continue;
}

LogRecord g_lastLog;
i32 g_logCount = 0;

void RecordLog(const LogRecord& record) {
    g_lastLog = record;
    ++g_logCount;
}

} // namespace

NX_TEST(AssertHook, ReceivesFailedAssertions) {
    // The test runner has its own handler installed; it is restored at the end.
    g_assertCount = 0;
    const AssertHandler runnerHandler = SetAssertHandler(&RecordAssert);

    NX_ASSERT(1 + 1 == 2, "never fires");
    NX_CHECK(g_assertCount == 0);

#if NX_ASSERTS_ENABLED
    const u32 line = NX_SOURCE_LOCATION.line + 1;
    NX_ASSERT(g_assertCount < 0, "expected failure");
    NX_CHECK(g_assertCount == 1);
    NX_CHECK(g_lastAssert.kind == AssertKind::Assertion);
    NX_CHECK(g_lastAssert.location.line == line);
    NX_CHECK(g_lastAssert.message[0] == 'e');
    NX_CHECK(g_lastAssert.condition[0] == 'g');
#endif

    NX_CHECK(SetAssertHandler(runnerHandler) == &RecordAssert);
}

NX_TEST(AssertHook, VerifyAlwaysEvaluates) {
    i32 sideEffects = 0;
    NX_VERIFY(++sideEffects == 1);
    NX_CHECK(sideEffects == 1);
}

NX_TEST(LogHook, ForwardsRecordsToTheInstalledSink) {
    g_logCount = 0;
    const LogSink previous = SetLogSink(&RecordLog);

    const char text[] = "hello";
    WriteLog(LogRecord{LogLevel::Warning, text, 5, NX_SOURCE_LOCATION});
    NX_CHECK(g_logCount == 1);
    NX_CHECK(g_lastLog.level == LogLevel::Warning);
    NX_CHECK(g_lastLog.length == 5);

    NX_CHECK(SetLogSink(previous) == &RecordLog);
}

NX_TEST(LogHook, DropsRecordsWithoutASink) {
    g_logCount = 0;
    const LogSink previous = SetLogSink(nullptr);
    const char text[] = "dropped";
    WriteLog(LogRecord{LogLevel::Info, text, 7, NX_SOURCE_LOCATION});
    NX_CHECK(g_logCount == 0);
    static_cast<void>(SetLogSink(previous));
}
