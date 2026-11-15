#pragma once

#include <Nexyl/Base/FloatingPoint.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>

// Scalar operations with the same result on every platform (docs/NEXYL_ARCHITECTURE.md, section 7.4). They are
// built from comparisons, bit operations and the correctly rounded primitives only, never from platform math.

namespace Nexyl::Math {

/// Satisfied by the two floating-point types of the engine, f32 and f64.
/// @since 0.1
template <typename T>
concept Real = SameAs<T, f32> || SameAs<T, f64>;

namespace Detail {

/// Bit layout of an IEEE-754 type. @since 0.1
template <typename T>
struct FloatLayout;

/// Bit layout of binary32. @since 0.1
template <>
struct FloatLayout<f32> {
    /// The integer of the same size.
    using Bits = u32;
    /// Number of explicit mantissa bits.
    static constexpr i32 kMantissaBits = 23;
    /// The exponent bias.
    static constexpr i32 kBias = 127;
    /// The sign bit.
    static constexpr Bits kSignMask = 0x8000'0000U;
    /// The exponent field.
    static constexpr Bits kExponentMask = 0x7F80'0000U;
    /// The mantissa field.
    static constexpr Bits kMantissaMask = 0x007F'FFFFU;
};

/// Bit layout of binary64. @since 0.1
template <>
struct FloatLayout<f64> {
    /// The integer of the same size.
    using Bits = u64;
    /// Number of explicit mantissa bits.
    static constexpr i32 kMantissaBits = 52;
    /// The exponent bias.
    static constexpr i32 kBias = 1023;
    /// The sign bit.
    static constexpr Bits kSignMask = 0x8000'0000'0000'0000ULL;
    /// The exponent field.
    static constexpr Bits kExponentMask = 0x7FF0'0000'0000'0000ULL;
    /// The mantissa field.
    static constexpr Bits kMantissaMask = 0x000F'FFFF'FFFF'FFFFULL;
};

/// Returns the bits of a value. @since 0.1
template <Real T>
[[nodiscard]] constexpr typename FloatLayout<T>::Bits ToBits(T value) {
    return BitCast<typename FloatLayout<T>::Bits>(value);
}

/// Returns the value of bits. @since 0.1
template <Real T>
[[nodiscard]] constexpr T FromBits(typename FloatLayout<T>::Bits bits) {
    return BitCast<T>(bits);
}

/// Returns `ifTrue` if `condition` holds, otherwise `ifFalse`. A floating-point value is selected through a mask on
/// its bits: MSVC (19.51, /O2) recognises `b < a ? b : a` as a minimum and, when it folds constant operands, returns
/// the other of two zeros than the code it generates, so the same call would give different bits depending on
/// whether it was folded. @since 0.1
template <typename T>
[[nodiscard]] constexpr T Select(bool condition, T ifTrue, T ifFalse) {
    if constexpr (Real<T>) {
        using Bits = typename FloatLayout<T>::Bits;
        const Bits mask = static_cast<Bits>(0) - static_cast<Bits>(condition);
        return FromBits<T>((ToBits(ifTrue) & mask) | (ToBits(ifFalse) & ~mask));
    } else {
        return condition ? ifTrue : ifFalse;
    }
}

} // namespace Detail

/// pi. @since 0.1
template <Real T>
inline constexpr T kPi = static_cast<T>(0x1.921fb54442d18p+1);
/// pi / 2. @since 0.1
template <Real T>
inline constexpr T kHalfPi = static_cast<T>(0x1.921fb54442d18p+0);
/// pi / 4. @since 0.1
template <Real T>
inline constexpr T kQuarterPi = static_cast<T>(0x1.921fb54442d18p-1);
/// 2 pi. @since 0.1
template <Real T>
inline constexpr T kTwoPi = static_cast<T>(0x1.921fb54442d18p+2);

/// Returns the quiet NaN this module produces for undefined results. @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr T QuietNaN() {
    using Layout = Detail::FloatLayout<T>;
    return Detail::FromBits<T>(Layout::kExponentMask | (Layout::kMantissaMask & ~(Layout::kMantissaMask >> 1U)));
}

/// Returns positive infinity. @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr T Infinity() {
    return Detail::FromBits<T>(Detail::FloatLayout<T>::kExponentMask);
}

/// Checks for NaN. @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr bool IsNaN(T value) {
    using Layout = Detail::FloatLayout<T>;
    const auto bits = Detail::ToBits(value);
    return (bits & Layout::kExponentMask) == Layout::kExponentMask && (bits & Layout::kMantissaMask) != 0;
}

/// Checks for positive or negative infinity. @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr bool IsInfinite(T value) {
    using Layout = Detail::FloatLayout<T>;
    return (Detail::ToBits(value) & ~Layout::kSignMask) == Layout::kExponentMask;
}

/// Checks that a value is neither NaN nor infinite. @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr bool IsFinite(T value) {
    using Layout = Detail::FloatLayout<T>;
    return (Detail::ToBits(value) & Layout::kExponentMask) != Layout::kExponentMask;
}

/// Returns the absolute value by clearing the sign bit, also for zero and NaN.
/// @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr T Abs(T value) {
    return Detail::FromBits<T>(Detail::ToBits(value) & ~Detail::FloatLayout<T>::kSignMask);
}

/// Returns `magnitude` with the sign of `sign`. @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr T CopySign(T magnitude, T sign) {
    using Layout = Detail::FloatLayout<T>;
    return Detail::FromBits<T>((Detail::ToBits(magnitude) & ~Layout::kSignMask) |
                               (Detail::ToBits(sign) & Layout::kSignMask));
}

/// Returns the smaller value by comparison and selection (rule DET-4). If either is NaN, returns `first`; of two
/// zeros, returns `first`. @threadsafety safe @determinism deterministic @since 0.1
template <typename T>
[[nodiscard]] constexpr T Min(T first, T second) {
    return Detail::Select(second < first, second, first);
}

/// Returns the larger value by comparison and selection (rule DET-4). If either is NaN, returns `first`; of two
/// zeros, returns `first`. @threadsafety safe @determinism deterministic @since 0.1
template <typename T>
[[nodiscard]] constexpr T Max(T first, T second) {
    return Detail::Select(first < second, second, first);
}

/// Limits a value to [low, high] by comparison and selection (rule DET-4). NaN stays NaN.
/// @pre `low` <= `high`. @threadsafety safe @determinism deterministic @since 0.1
template <typename T>
[[nodiscard]] constexpr T Clamp(T value, T low, T high) {
    return Detail::Select(value < low, low, Detail::Select(high < value, high, value));
}

/// Rounds toward zero. Keeps the sign of zero; infinities and NaN pass through.
/// @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr T Trunc(T value) {
    using Layout = Detail::FloatLayout<T>;
    using Bits = typename Layout::Bits;
    const Bits bits = Detail::ToBits(value);
    const i32 exponent =
        static_cast<i32>((bits & Layout::kExponentMask) >> static_cast<u32>(Layout::kMantissaBits)) - Layout::kBias;
    if (exponent >= Layout::kMantissaBits) {
        return value; // Already an integer, or infinite, or NaN.
    }
    if (exponent < 0) {
        return Detail::FromBits<T>(bits & Layout::kSignMask);
    }
    const Bits fraction = Layout::kMantissaMask >> static_cast<u32>(exponent);
    return Detail::FromBits<T>(bits & ~fraction);
}

/// Rounds toward negative infinity. @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr T Floor(T value) {
    const T truncated = Trunc(value);
    return value < truncated ? truncated - static_cast<T>(1) : truncated;
}

/// Rounds toward positive infinity. @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr T Ceil(T value) {
    const T truncated = Trunc(value);
    return truncated < value ? truncated + static_cast<T>(1) : truncated;
}

/// Rounds to the nearest integer, halfway cases away from zero. @threadsafety safe @determinism deterministic
/// @since 0.1
template <Real T>
[[nodiscard]] constexpr T Round(T value) {
    const T truncated = Trunc(value);
    // Exact: the difference of a value and its integer part fits the format.
    const T fraction = value - truncated;
    if (Abs(fraction) < static_cast<T>(0.5)) {
        return truncated;
    }
    return truncated + CopySign(static_cast<T>(1), value);
}

/// Converts to i32 by truncation, saturating at the limits; NaN gives zero (rule DET-4). Plain conversion of an
/// out-of-range value is undefined behaviour and differs between x86 and ARM.
/// @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr i32 SaturateToI32(T value) {
    if (IsNaN(value)) {
        return 0;
    }
    const T truncated = Trunc(value);
    if (!(truncated < static_cast<T>(2147483648.0))) {
        return 2147483647;
    }
    if (truncated < static_cast<T>(-2147483648.0)) {
        return -2147483647 - 1;
    }
    return static_cast<i32>(truncated);
}

/// Converts to i64 by truncation, saturating at the limits; NaN gives zero (rule DET-4).
/// @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr i64 SaturateToI64(T value) {
    if (IsNaN(value)) {
        return 0;
    }
    const T truncated = Trunc(value);
    if (!(truncated < static_cast<T>(9223372036854775808.0))) {
        return 9223372036854775807LL;
    }
    if (truncated < static_cast<T>(-9223372036854775808.0)) {
        return -9223372036854775807LL - 1;
    }
    return static_cast<i64>(truncated);
}

/// Returns the correctly rounded square root, the one non-arithmetic primitive of rule DET-1. Negative values
/// give NaN. @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] inline T Sqrt(T value) {
    return SquareRoot(value);
}

/// Interpolates linearly: `first + (second - first) * fraction`, in that order.
/// @threadsafety safe @determinism deterministic @since 0.1
template <Real T>
[[nodiscard]] constexpr T Lerp(T first, T second, T fraction) {
    return first + (second - first) * fraction;
}

} // namespace Nexyl::Math
