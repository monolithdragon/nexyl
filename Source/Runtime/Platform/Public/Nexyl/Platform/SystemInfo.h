#pragma once

#include <Nexyl/Base/Types.h>

namespace Nexyl::Platform {

/// Static facts about the machine and the operating system's memory system.
/// @threadsafety safe
/// @since 0.1
struct SystemInfo {
    /// Size of a virtual-memory page in bytes. Commit, decommit and protection work in whole pages.
    usize pageSize = 0;
    /// Alignment of every address-space reservation in bytes. At least the page size; 64 KiB on Windows.
    usize allocationGranularity = 0;
    /// Number of logical processors available to the process.
    u32 logicalProcessorCount = 0;
};

/// Returns the system information. The values do not change while the process runs.
///
/// @return The page size, allocation granularity and processor count.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API SystemInfo GetSystemInfo();

} // namespace Nexyl::Platform
