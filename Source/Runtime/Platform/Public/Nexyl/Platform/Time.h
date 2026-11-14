#pragma once

#include <Nexyl/Base/Types.h>

namespace Nexyl::Platform {

/// Returns the time of a monotonic clock in nanoseconds.
///
/// The clock never goes backwards and is not affected by changes to the system time. Its origin is
/// unspecified, so only differences between two readings are meaningful. This is wall-clock time:
/// simulation code MUST NOT call it (rule DET-6) and uses `SimTime` instead.
///
/// @return Nanoseconds since an unspecified origin, or 0 if the platform clock is unavailable.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API u64 GetMonotonicNanoseconds();

} // namespace Nexyl::Platform
