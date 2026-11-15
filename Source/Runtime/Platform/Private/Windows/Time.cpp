#include <Nexyl/Platform/Time.h>

#include <Windows.h>

namespace Nexyl::Platform {

u64 GetMonotonicNanoseconds() {
    LARGE_INTEGER frequency = {};
    LARGE_INTEGER counter = {};
    if (::QueryPerformanceFrequency(&frequency) == 0 || ::QueryPerformanceCounter(&counter) == 0 ||
        frequency.QuadPart <= 0) {
        return 0;
    }

    constexpr u64 kNanosecondsPerSecond = 1'000'000'000;
    const u64 ticks = static_cast<u64>(counter.QuadPart);
    const u64 ticksPerSecond = static_cast<u64>(frequency.QuadPart);

    // Whole seconds and the remainder are converted separately, so the multiplication cannot overflow.
    return (ticks / ticksPerSecond) * kNanosecondsPerSecond +
           (ticks % ticksPerSecond) * kNanosecondsPerSecond / ticksPerSecond;
}

} // namespace Nexyl::Platform
