#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Memory {

/// Errors reported by the Memory module.
/// @since 0.1
enum class MemoryError : u16 {
    /// The operating system or the parent allocator cannot provide more memory.
    OutOfMemory = 1,
    /// An allocator with a fixed capacity has no room for the request, although the system may have memory.
    CapacityExceeded,
};

/// Associates MemoryError with the Memory module (required by the ErrorEnum concept).
///
/// @return ModuleId::Memory.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] constexpr ModuleId GetErrorModule(MemoryError /*code*/) noexcept {
    return ModuleId::Memory;
}

} // namespace Nexyl::Memory
