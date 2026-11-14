#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>

namespace Nexyl::Memory {

/// Alignment of a block when the caller does not ask for more: enough for every fundamental type and for 16-byte
/// SIMD vectors.
/// @since 0.1
inline constexpr usize kDefaultAlignment = 16;

/// Rounds a value up to a multiple of `alignment`.
/// @param value     The value.
/// @param alignment A power of two.
/// @pre The result fits in a usize.
/// @return The smallest multiple of `alignment` that is not below `value`.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] constexpr usize AlignUp(usize value, usize alignment) {
    NX_ASSERT(IsPowerOfTwo(alignment), "Alignment must be a power of two.");
    NX_ASSERT(value <= ~usize{0} - (alignment - 1), "AlignUp overflows.");
    return (value + alignment - 1) & ~(alignment - 1);
}

/// Rounds a value down to a multiple of `alignment`.
/// @param value     The value.
/// @param alignment A power of two.
/// @return The largest multiple of `alignment` that is not above `value`.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] constexpr usize AlignDown(usize value, usize alignment) {
    NX_ASSERT(IsPowerOfTwo(alignment), "Alignment must be a power of two.");
    return value & ~(alignment - 1);
}

/// Returns the numeric value of an address, for alignment and range arithmetic.
/// @param address The address.
/// @return Its value.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] inline usize ToAddress(const void* address) {
    return BitCast<usize>(address);
}

/// Checks whether an address is a multiple of `alignment`.
/// @param address   The address.
/// @param alignment A power of two.
/// @return True if it is aligned.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] inline bool IsAligned(const void* address, usize alignment) {
    return AlignDown(ToAddress(address), alignment) == ToAddress(address);
}

/// Moves a pointer forward to the next multiple of `alignment`, staying inside the same object.
/// @param address   The address.
/// @param alignment A power of two.
/// @return `address` itself if aligned, otherwise the next aligned address.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] inline u8* AlignUp(u8* address, usize alignment) {
    return address + (AlignUp(ToAddress(address), alignment) - ToAddress(address));
}

} // namespace Nexyl::Memory
