#pragma once

#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>

// Hash functions for HashMap and HashSet (docs/NEXYL_ARCHITECTURE.md, section 7.7).
//
// The seed is fixed and the results are the same on every platform, so hash-ordered work is reproducible. The
// byte hash follows the published, public-domain wyhash algorithm by Wang Yi (decision D-31); this is an own
// implementation, not imported code.
//
// A type becomes a hash-map key by providing `u64 HashValue(const T&)` in its own namespace, where
// argument-dependent lookup finds it, together with `operator==`.

namespace Nexyl::Containers {

/// The seed every hash uses unless the caller passes another. Fixed, never randomised per process.
/// @since 0.1
inline constexpr u64 kDefaultHashSeed = 0x9E3779B97F4A7C15;

namespace Detail {

/// The mixing constants of the hash. @since 0.1
inline constexpr u64 kHashSecret0 = 0x2D358DCCAA6C78A5;
/// The mixing constants of the hash. @since 0.1
inline constexpr u64 kHashSecret1 = 0x8BB84B93962EACC9;
/// The mixing constants of the hash. @since 0.1
inline constexpr u64 kHashSecret2 = 0x4B33A62ED433D4A3;
/// The mixing constants of the hash. @since 0.1
inline constexpr u64 kHashSecret3 = 0x4D5A2DA51DE1AA47;

/// Multiplies two values to 128 bits and folds the halves together: the core mixing step.
/// @param first  One value.
/// @param second The other value.
/// @return The low half of the product XOR its high half.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] inline u64 HashMix(u64 first, u64 second) {
    return (first * second) ^ MultiplyHigh(first, second);
}

} // namespace Detail

/// Hashes a range of bytes.
/// @param data Bytes to hash. May be null when `size` is zero.
/// @param size Number of bytes.
/// @param seed The seed.
/// @return The hash. Equal on every platform for equal bytes and seed.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_CONTAINERS_API u64 HashBytes(const void* data, usize size, u64 seed = kDefaultHashSeed);

/// Hashes a 64-bit value.
/// @param value The value.
/// @param seed  The seed.
/// @return The hash. Equal on every platform for equal value and seed.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] inline u64 HashInteger(u64 value, u64 seed = kDefaultHashSeed) {
    u64 first = value ^ Detail::kHashSecret0;
    u64 second = seed ^ Detail::kHashSecret1;
    const u64 low = first * second;
    const u64 high = MultiplyHigh(first, second);
    first = low;
    second = high;
    return Detail::HashMix(first ^ Detail::kHashSecret0, second ^ Detail::kHashSecret1);
}

/// Hashes an integer or an enumeration.
/// @param value The value.
/// @return The hash of its numeric value.
/// @threadsafety safe
/// @since 0.1
template <typename T>
    requires(Integral<T> || Enumeration<T>)
[[nodiscard]] u64 HashValue(T value) {
    return HashInteger(static_cast<u64>(value));
}

/// Hashes a pointer by its address. Addresses differ from run to run, so simulation state never uses pointer keys
/// (docs/NEXYL_ARCHITECTURE.md, section 7.7).
/// @param pointer The pointer.
/// @return The hash of its address.
/// @threadsafety safe
/// @since 0.1
template <typename T>
[[nodiscard]] u64 HashValue(T* pointer) {
    return HashInteger(static_cast<u64>(BitCast<usize>(pointer)));
}

/// Satisfied by a type that HashMap and HashSet can use as a key: it has a HashValue function and `==`.
/// @since 0.1
template <typename T>
concept HashableKey = requires(const T& first, const T& second) {
    { HashValue(first) } -> SameAs<u64>;
    { first == second } -> ConvertibleTo<bool>;
};

} // namespace Nexyl::Containers
