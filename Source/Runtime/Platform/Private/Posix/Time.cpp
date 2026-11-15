#include <Nexyl/Platform/Time.h>

#include <time.h>

namespace Nexyl::Platform {

u64 GetMonotonicNanoseconds() {
    timespec now = {};
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }

    constexpr u64 kNanosecondsPerSecond = 1'000'000'000;
    return static_cast<u64>(now.tv_sec) * kNanosecondsPerSecond + static_cast<u64>(now.tv_nsec);
}

} // namespace Nexyl::Platform
