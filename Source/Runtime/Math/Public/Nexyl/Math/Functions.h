#pragma once

#include <Nexyl/Base/Types.h>

// Transcendental functions with the same bits on every platform (docs/NEXYL_ARCHITECTURE.md, section 7.4; rule
// DET-1). Each is evaluated in f64 from + - * / and the square root, in a fixed order, then rounded once to f32.
// The result is within one unit in the last place of the exact value and is nearly always the correctly rounded
// one; the tests check both against reference values computed by mpmath (decision D-27). Angles are in radians.
// Special values follow IEEE-754: NaN in gives NaN out, and a result that is undefined gives NaN.

namespace Nexyl::Math {

/// The sine and the cosine of one angle, from SinCos.
/// @threadsafety safe
/// @since 0.1
struct SineCosine {
    /// The sine.
    f32 sine = 0.0f;
    /// The cosine.
    f32 cosine = 1.0f;
};

/// Returns the sine. @param angle In radians; any finite value. @return The sine; NaN for infinity.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Sin(f32 angle);

/// Returns the cosine. @param angle In radians; any finite value. @return The cosine; NaN for infinity.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Cos(f32 angle);

/// Returns the sine and the cosine, sharing one argument reduction. @param angle In radians.
/// @return Both values, equal to those of Sin and Cos.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API SineCosine SinCos(f32 angle);

/// Returns the tangent. @param angle In radians. @return The tangent; NaN for infinity.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Tan(f32 angle);

/// Returns the arcsine. @param value In [-1, 1]. @return An angle in [-pi/2, pi/2]; NaN outside the domain.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Asin(f32 value);

/// Returns the arccosine. @param value In [-1, 1]. @return An angle in [0, pi]; NaN outside the domain.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Acos(f32 value);

/// Returns the arctangent. @param value Any value. @return An angle in [-pi/2, pi/2].
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Atan(f32 value);

/// Returns the angle of the point (x, y) from the positive x axis.
/// @param y The second coordinate.
/// @param x The first coordinate.
/// @return An angle in [-pi, pi], with the IEEE-754 results for zeros and infinities.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Atan2(f32 y, f32 x);

/// Returns e raised to `value`. @return The power; infinity above about 88.72, zero below about -103.97.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Exp(f32 value);

/// Returns 2 raised to `value`; exact for integers in the f32 range.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Exp2(f32 value);

/// Returns the natural logarithm. @return The logarithm; minus infinity for zero, NaN for negative values.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Log(f32 value);

/// Returns the base-2 logarithm; exact for powers of two. @return As for Log.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Log2(f32 value);

/// Returns `base` raised to `exponent`, with the special cases of IEEE-754 pow.
/// @param base     Any value; a negative base needs an integer exponent, otherwise the result is NaN.
/// @param exponent Any value.
/// @return The power.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Pow(f32 base, f32 exponent);

/// Returns the real cube root, negative for negative values; exact for perfect cubes.
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Cbrt(f32 value);

/// Returns the hyperbolic tangent, in [-1, 1].
/// @threadsafety safe @determinism deterministic @since 0.1
[[nodiscard]] NX_MATH_API f32 Tanh(f32 value);

} // namespace Nexyl::Math
