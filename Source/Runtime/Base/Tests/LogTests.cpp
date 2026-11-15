#include <Nexyl/Base/Log.h>
#include <Nexyl/Base/LogHook.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

namespace {

struct Captured {
    i32 count = 0;
    LogLevel level = LogLevel::Info;
    char text[kMaxLogMessageSize] = {};
    usize length = 0;
    u32 line = 0;
};

void Capture(void* context, const LogRecord& record) {
    auto& captured = *static_cast<Captured*>(context);
    ++captured.count;
    captured.level = record.level;
    captured.length = record.length;
    captured.line = record.location.line;
    for (usize index = 0; index < record.length && index < kMaxLogMessageSize; ++index) {
        captured.text[index] = record.text[index];
    }
}

[[nodiscard]] bool TextEquals(const Captured& captured, const char* expected) {
    usize length = 0;
    while (expected[length] != '\0') {
        ++length;
    }
    if (length != captured.length) {
        return false;
    }
    for (usize index = 0; index < length; ++index) {
        if (captured.text[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

// Installs a capturing binding for one test and restores the previous state.
class ScopedCapture final {
public:
    ScopedCapture() : m_binding{&Capture, &captured}, m_previous(SetLogSink(&m_binding)) {}
    ~ScopedCapture() {
        static_cast<void>(SetLogSink(m_previous));
        SetLogMinimumLevel(LogLevel::Trace);
    }
    ScopedCapture(const ScopedCapture&) = delete;
    ScopedCapture& operator=(const ScopedCapture&) = delete;
    ScopedCapture(ScopedCapture&&) = delete;
    ScopedCapture& operator=(ScopedCapture&&) = delete;

    Captured captured;

private:
    LogSinkBinding m_binding;
    const LogSinkBinding* m_previous = nullptr;
};

bool g_argumentEvaluated = false;

i32 Evaluate() {
    g_argumentEvaluated = true;
    return 1;
}

} // namespace

NX_TEST(Log, FormatsAndForwardsWithTheCallersLocation) {
    const ScopedCapture scope;
    const u32 line = SourceLocation::Current().line + 1;
    LogWarning("{} of {} done", 3, 4);
    NX_CHECK(scope.captured.count == 1);
    NX_CHECK(scope.captured.level == LogLevel::Warning);
    NX_CHECK(scope.captured.line == line);
    NX_CHECK(TextEquals(scope.captured, "3 of 4 done"));
}

NX_TEST(Log, EachFunctionUsesItsLevel) {
    const ScopedCapture scope;
    LogTrace("t");
    NX_CHECK(scope.captured.level == LogLevel::Trace);
    LogDebug("d");
    NX_CHECK(scope.captured.level == LogLevel::Debug);
    LogInfo("i");
    NX_CHECK(scope.captured.level == LogLevel::Info);
    LogError("e");
    NX_CHECK(scope.captured.level == LogLevel::Error);
    LogFatal("f");
    NX_CHECK(scope.captured.level == LogLevel::Fatal);
    NX_CHECK(scope.captured.count == 5);
}

NX_TEST(Log, SkipsDisabledLevels) {
    const ScopedCapture scope;
    SetLogMinimumLevel(LogLevel::Error);
    LogInfo("hidden {}", 1);
    NX_CHECK(scope.captured.count == 0);
    LogError("shown");
    NX_CHECK(scope.captured.count == 1);
}

NX_TEST(Log, ArgumentsAreEvaluatedEvenWhenTheLevelIsDisabled) {
    // Plain functions, not macros: arguments are evaluated before the level check (rule CLS-12).
    const ScopedCapture scope;
    SetLogMinimumLevel(LogLevel::Off);
    g_argumentEvaluated = false;
    LogInfo("{}", Evaluate());
    NX_CHECK(g_argumentEvaluated);
    NX_CHECK(scope.captured.count == 0);
}

NX_TEST(Log, CutsLongMessagesWithAMarker) {
    const ScopedCapture scope;
    char longText[kMaxLogMessageSize + 100] = {};
    for (char& character : longText) {
        character = 'x';
    }
    longText[sizeof(longText) - 1] = '\0';
    LogInfo("{}", longText);
    NX_REQUIRE(scope.captured.length == kMaxLogMessageSize);
    NX_CHECK(scope.captured.text[kMaxLogMessageSize - 4] == 'x');
    NX_CHECK(scope.captured.text[kMaxLogMessageSize - 3] == '.');
    NX_CHECK(scope.captured.text[kMaxLogMessageSize - 1] == '.');
}

NX_TEST(Log, DoesNotSplitAUtf8SequenceWhenCutting) {
    const ScopedCapture scope;
    // "ab" then two-byte sequences (U+00E9) from an even offset: the cut at byte 509 falls inside one of them.
    char text[kMaxLogMessageSize + 64] = {};
    text[0] = 'a';
    text[1] = 'b';
    for (usize index = 2; index + 1 < sizeof(text) - 1; index += 2) {
        text[index] = static_cast<char>(0xC3);
        text[index + 1] = static_cast<char>(0xA9);
    }
    LogInfo("{}", text);
    const usize markerStart = scope.captured.length - 3;
    NX_CHECK(scope.captured.text[markerStart] == '.');
    NX_CHECK(static_cast<u8>(scope.captured.text[markerStart - 1]) == 0xA9U);
    NX_CHECK(scope.captured.length < kMaxLogMessageSize);
}
