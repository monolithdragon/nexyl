#pragma once

#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Hash.h>

namespace Nexyl::Containers {

/// Refers to an object in a HandlePool by slot index and generation (docs/CODING_STANDARDS.md, section 6.1).
///
/// A handle stays a plain value: it can be copied, stored, serialised and compared, and it never dangles. Once
/// its object is removed, the slot's generation moves on and the pool reports the handle as stale. Generation
/// zero marks the null handle. References between systems are handles, not pointers (rule OWN-4).
/// @threadsafety safe
/// @since 0.1
template <typename T>
struct Handle {
    /// Slot index in the pool.
    u32 index = 0;
    /// Generation of the slot when the handle was made; zero for the null handle.
    u32 generation = 0;

    /// Checks whether this is the null handle, which never refers to an object. @since 0.1
    [[nodiscard]] constexpr bool IsNull() const {
        return generation == 0;
    }

    /// Compares two handles. @since 0.1
    constexpr bool operator==(const Handle&) const = default;
};

/// Hashes a handle, so that handles can be HashMap keys.
/// @param handle The handle.
/// @return The hash.
/// @threadsafety safe
/// @since 0.1
template <typename T>
[[nodiscard]] u64 HashValue(const Handle<T>& handle) {
    return HashInteger((static_cast<u64>(handle.generation) << 32U) | handle.index);
}

} // namespace Nexyl::Containers
