#include <Nexyl/Test/Runner.h>
#include <Nexyl/Test/Test.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Platform/Console.h>
#include <Nexyl/Platform/Time.h>

namespace Nexyl::Test {

namespace {

using Platform::ConsoleStream;

// ---- Text helpers (no C runtime) ---------------------------------------------------------------------------

usize TextLength(const char* text) {
    usize length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

bool TextEquals(const char* first, const char* second) {
    usize index = 0;
    while (first[index] != '\0' && first[index] == second[index]) {
        ++index;
    }
    return first[index] == second[index];
}

bool TextContains(Span<const char> text, Span<const char> pattern) {
    if (pattern.Size() > text.Size()) {
        return false;
    }
    for (usize start = 0; start + pattern.Size() <= text.Size(); ++start) {
        usize matched = 0;
        while (matched < pattern.Size() && text[start + matched] == pattern[matched]) {
            ++matched;
        }
        if (matched == pattern.Size()) {
            return true;
        }
    }
    return false;
}

/// Collects output in a fixed buffer and writes it to the console in as few calls as possible.
class Writer final {
public:
    Writer(ConsoleStream stream, bool enabled) : m_stream(stream), m_enabled(enabled) {}

    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer(Writer&&) = delete;
    Writer& operator=(Writer&&) = delete;

    ~Writer() {
        Flush();
    }

    Writer& Text(const char* text) {
        return Text(Span<const char>(text, TextLength(text)));
    }

    Writer& Text(Span<const char> text) {
        if (!m_enabled) {
            return *this;
        }
        for (const char character : text) {
            if (m_size == kCapacity) {
                Flush();
            }
            m_buffer[m_size++] = character;
        }
        return *this;
    }

    Writer& Decimal(u64 value) {
        char digits[20] = {};
        usize count = 0;
        do {
            digits[count++] = static_cast<char>('0' + static_cast<char>(value % 10));
            value /= 10;
        } while (value != 0);

        char ordered[20] = {};
        for (usize index = 0; index < count; ++index) {
            ordered[index] = digits[count - 1 - index];
        }
        return Text(Span<const char>(ordered, count));
    }

    void Flush() {
        if (m_size != 0) {
            Platform::WriteToConsole(m_stream, Span<const char>(m_buffer, m_size));
            m_size = 0;
        }
    }

private:
    static constexpr usize kCapacity = 512;

    char m_buffer[kCapacity] = {};
    usize m_size = 0;
    ConsoleStream m_stream;
    bool m_enabled;
};

// ---- Run state ---------------------------------------------------------------------------------------------

struct TestState {
    u32 failedChecks = 0;
    bool quiet = false;
};

// The test running on the main thread, or null. Constant-initialized (rule INIT-1).
constinit TestState* g_currentTest = nullptr;

AssertAction ReportAssertion(const AssertInfo& info) {
    Writer writer(ConsoleStream::Error, true);
    writer.Text(info.location.file).Text(":").Decimal(info.location.line).Text(": ");
    switch (info.kind) {
        case AssertKind::Assertion:
            writer.Text("assertion failed: ").Text(info.condition);
            break;
        case AssertKind::Unreachable:
            writer.Text("unreachable code reached");
            break;
        case AssertKind::Fatal:
            writer.Text("fatal error");
            break;
    }
    if (info.message[0] != '\0') {
        writer.Text(" (").Text(info.message).Text(")");
    }
    writer.Text("\nThe test run stops here, because the code under test cannot continue safely.\n");
    writer.Flush();
    return AssertAction::Abort;
}

void WriteFullName(Writer& writer, const TestCase& test) {
    writer.Text(test.suite).Text(".").Text(test.name);
}

bool IsSelected(const TestCase& test, const char* filter) {
    if (filter == nullptr) {
        return true;
    }
    // The full name is assembled in a fixed buffer; longer names are compared by their prefix.
    char fullName[256] = {};
    usize length = 0;
    const auto append = [&](const char* text) {
        for (usize index = 0; text[index] != '\0' && length < sizeof(fullName); ++index) {
            fullName[length++] = text[index];
        }
    };
    append(test.suite);
    append(".");
    append(test.name);
    return TextContains(Span<const char>(fullName, length), Span<const char>(filter, TextLength(filter)));
}

void WriteUsage() {
    Writer writer(ConsoleStream::Error, true);
    writer.Text("Usage: <test executable> [--filter <text>] [--list] [--help]\n"
                "  --filter <text>  run only tests whose Suite.Name contains <text>\n"
                "  --list           print the selected test names instead of running them\n");
}

} // namespace

namespace Detail {

void ReportCheckFailure(const char* expression, const SourceLocation& location) {
    TestState* const state = g_currentTest;
    if (state == nullptr) {
        NX_FATAL("NX_CHECK or NX_REQUIRE was used outside a running test.");
    }
    ++state->failedChecks;

    Writer writer(ConsoleStream::Output, !state->quiet);
    writer.Text(location.file).Text(":").Decimal(location.line).Text(": check failed: ").Text(expression).Text("\n");
}

} // namespace Detail

RunSummary RunTests(Span<const TestCase> tests, const RunOptions& options) {
    RunSummary summary;
    TestState* const outerTest = g_currentTest;
    const AssertHandler previousHandler = SetAssertHandler(&ReportAssertion);

    for (const TestCase& test : tests) {
        if (!IsSelected(test, options.filter)) {
            continue;
        }

        Writer writer(ConsoleStream::Output, !options.quiet);
        if (options.listOnly) {
            WriteFullName(writer, test);
            writer.Text("\n");
            continue;
        }

        writer.Text("[ RUN      ] ");
        WriteFullName(writer, test);
        writer.Text("\n");
        writer.Flush();

        TestState state;
        state.quiet = options.quiet;
        g_currentTest = &state;
        const u64 start = Platform::GetMonotonicNanoseconds();
        test.function();
        const u64 elapsedMilliseconds = (Platform::GetMonotonicNanoseconds() - start) / 1'000'000;
        g_currentTest = outerTest;

        ++summary.ran;
        if (state.failedChecks == 0) {
            writer.Text("[       OK ] ");
            WriteFullName(writer, test);
            writer.Text(" (").Decimal(elapsedMilliseconds).Text(" ms)\n");
        } else {
            ++summary.failed;
            writer.Text("[  FAILED  ] ");
            WriteFullName(writer, test);
            writer.Text(" (").Decimal(state.failedChecks).Text(" failed check");
            writer.Text(state.failedChecks == 1 ? "" : "s").Text(", ").Decimal(elapsedMilliseconds).Text(" ms)\n");
        }
    }

    NX_VERIFY(SetAssertHandler(previousHandler) == &ReportAssertion);
    return summary;
}

int RunTestsMain(Span<const TestCase> tests, int argumentCount, const char* const* arguments) {
    RunOptions options;
    for (int index = 1; index < argumentCount; ++index) {
        const char* const argument = arguments[index];
        if (TextEquals(argument, "--filter") && index + 1 < argumentCount) {
            options.filter = arguments[++index];
        } else if (TextEquals(argument, "--list")) {
            options.listOnly = true;
        } else {
            WriteUsage();
            return TextEquals(argument, "--help") ? 0 : 2;
        }
    }

    Writer writer(ConsoleStream::Output, true);
    if (tests.IsEmpty()) {
        writer.Text("No tests are registered. Define tests with NX_TEST in the module's Tests/ directory.\n");
        return 1;
    }

    const RunSummary summary = RunTests(tests, options);
    if (options.listOnly) {
        return 0;
    }
    if (summary.ran == 0) {
        writer.Text("No test matched the filter.\n");
        return 1;
    }

    writer.Text("\n").Decimal(summary.ran).Text(summary.ran == 1 ? " test ran, " : " tests ran, ");
    writer.Decimal(summary.failed).Text(" failed.\n");
    return summary.failed == 0 ? 0 : 1;
}

} // namespace Nexyl::Test
