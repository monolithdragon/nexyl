#pragma once

#include <Nexyl/Base/Traits.h>
#include <Nexyl/Memory/IAllocator.h>

// Explicit copies of container elements (rule OWN-7). Owning containers are not copyable; they provide
// `Clone(IAllocator&)` instead. A container's Clone copies each element with the element's own Clone where it has
// one, so a container of containers is copied deeply into the target allocator.

namespace Nexyl::Containers {

/// Satisfied by a type that copies itself explicitly into memory from a given allocator.
/// @since 0.1
template <typename T>
concept ExplicitlyCloneable = requires(const T& value, Memory::IAllocator& allocator) {
    { value.Clone(allocator) } -> SameAs<T>;
};

/// Satisfied by a type that a container can copy: copyable, or explicitly cloneable.
/// @since 0.1
template <typename T>
concept Cloneable = CopyConstructible<T> || ExplicitlyCloneable<T>;

namespace Detail {

/// Copies one element for a container's Clone, preferring the element's own Clone.
/// @param value     The element.
/// @param allocator The allocator of the copy.
/// @return The copy.
/// @threadsafety compatible
/// @since 0.1
template <Cloneable T>
[[nodiscard]] T CloneValue(const T& value, Memory::IAllocator& allocator) {
    if constexpr (ExplicitlyCloneable<T>) {
        return value.Clone(allocator);
    } else {
        static_cast<void>(allocator);
        return T(value);
    }
}

} // namespace Detail

} // namespace Nexyl::Containers
