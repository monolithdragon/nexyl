#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Platform/Console.h>
#include <Nexyl/Platform/Time.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

NX_TEST(MonotonicClock, NeverGoesBackwardsAndAdvances) {
    const u64 first = Platform::GetMonotonicNanoseconds();
    NX_REQUIRE(first != 0);

    u64 previous = first;
    bool advanced = false;
    for (u32 iteration = 0; iteration < 10'000'000 && !advanced; ++iteration) {
        const u64 current = Platform::GetMonotonicNanoseconds();
        NX_REQUIRE(current >= previous);
        advanced = current > first;
        previous = current;
    }
    NX_CHECK(advanced);
}

NX_TEST(Console, WritesNormalAndEmptyText) {
    // Output cannot be read back portably; the test proves the calls complete for normal and empty text.
    // Byte-exact output was verified by redirecting the test executable's output.
    const char text[] = "Platform console test line\n";
    Platform::WriteToConsole(Platform::ConsoleStream::Output, Span<const char>(text, sizeof(text) - 1));
    Platform::WriteToConsole(Platform::ConsoleStream::Error, Span<const char>());
}
