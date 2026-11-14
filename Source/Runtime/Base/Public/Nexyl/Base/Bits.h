#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Std/Bit.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>

// Bit utilities over unsigned integers. They compile to single instructions where the CPU has them.

namespace Nexyl {

/// Satisfied by the unsigned integer types, including `usize` and excluding `bool` and character types.
/// @since 0.1
template <typename T>
concept UnsignedInteger = SameAs<T, unsigned char> || SameAs<T, unsigned short> || SameAs<T, unsigned int> ||
                          SameAs<T, unsigned long> || SameAs<T, unsigned long long>;

/// Checks whether exactly one bit is set.
/// @param value The value.
/// @return True for 1, 2, 4, 8 and so on; false for zero.
/// @threadsafety safe
/// @since 0.1
template <UnsignedInteger T>
[[nodiscard]] constexpr bool IsPowerOfTwo(T value) {
    return std::has_single_bit(value);
}

/// Counts the zero bits above the highest set bit.
/// @param value The value.
/// @return The count; the width of `T` in bits for zero.
/// @threadsafety safe
/// @since 0.1
template <UnsignedInteger T>
[[nodiscard]] constexpr u32 CountLeadingZeros(T value) {
    return static_cast<u32>(std::countl_zero(value));
}

/// Counts the zero bits below the lowest set bit.
/// @param value The value.
/// @return The count; the width of `T` in bits for zero.
/// @threadsafety safe
/// @since 0.1
template <UnsignedInteger T>
[[nodiscard]] constexpr u32 CountTrailingZeros(T value) {
    return static_cast<u32>(std::countr_zero(value));
}

/// Returns the index of the highest set bit, counted from bit 0.
/// @param value The value.
/// @pre `value` is not zero.
/// @return The index: 0 for 1, 3 for 8 and for 15.
/// @threadsafety safe
/// @since 0.1
template <UnsignedInteger T>
[[nodiscard]] constexpr u32 HighestSetBit(T value) {
    NX_ASSERT(value != 0, "HighestSetBit of zero is undefined.");
    return static_cast<u32>(std::bit_width(value)) - 1;
}

} // namespace Nexyl
