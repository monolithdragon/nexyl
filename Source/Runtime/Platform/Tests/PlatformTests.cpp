// Unit tests for the Platform module.
//
// TODO(phase-1): move these onto the engine's Test module. Until then the file counts failed checks and
// returns the count as the exit code.

#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Platform/Console.h>
#include <Nexyl/Platform/Time.h>

namespace {

using namespace Nexyl;

i32 g_failures = 0;

void Check(bool condition) {
    if (!condition) {
        ++g_failures;
    }
}

void TestMonotonicClock() {
    const u64 first = Platform::GetMonotonicNanoseconds();
    Check(first != 0);

    // The clock never goes backwards, and it advances within a bounded number of readings.
    u64 previous = first;
    bool advanced = false;
    for (u32 iteration = 0; iteration < 10'000'000 && !advanced; ++iteration) {
        const u64 current = Platform::GetMonotonicNanoseconds();
        Check(current >= previous);
        advanced = current > first;
        previous = current;
    }
    Check(advanced);
}

void TestConsole() {
    // Output cannot be read back portably; the test proves the calls complete for normal and empty text.
    const char text[] = "Platform console test line\n";
    Platform::WriteToConsole(Platform::ConsoleStream::Output, Span<const char>(text, sizeof(text) - 1));
    Platform::WriteToConsole(Platform::ConsoleStream::Error, Span<const char>());
}

} // namespace

int main() {
    TestMonotonicClock();
    TestConsole();
    return g_failures;
}
