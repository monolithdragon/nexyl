#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Math/Scalar.h>

namespace Nexyl::Math {

/// A 2D vector of f32, for texture coordinates, screen positions and planar quantities.
/// @threadsafety compatible @determinism deterministic @since 0.1
struct Vec2 {
    /// The first component.
    f32 x = 0.0f;
    /// The second component.
    f32 y = 0.0f;

    /// Compares exactly, component by component. @since 0.1
    constexpr bool operator==(const Vec2&) const = default;

    /// Negates every component. @determinism deterministic @since 0.1
    [[nodiscard]] constexpr Vec2 operator-() const {
        return Vec2{-x, -y};
    }
};

static_assert(sizeof(Vec2) == 8, "Vec2 is two packed f32.");

/// Adds component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec2 operator+(const Vec2& first, const Vec2& second) {
    return Vec2{first.x + second.x, first.y + second.y};
}

/// Subtracts component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec2 operator-(const Vec2& first, const Vec2& second) {
    return Vec2{first.x - second.x, first.y - second.y};
}

/// Scales every component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec2 operator*(const Vec2& vector, f32 factor) {
    return Vec2{vector.x * factor, vector.y * factor};
}

/// Scales every component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec2 operator*(f32 factor, const Vec2& vector) {
    return vector * factor;
}

/// Divides every component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec2 operator/(const Vec2& vector, f32 divisor) {
    return Vec2{vector.x / divisor, vector.y / divisor};
}

/// Returns the dot product, summed in order x, y. @determinism deterministic @since 0.1
[[nodiscard]] constexpr f32 Dot(const Vec2& first, const Vec2& second) {
    return first.x * second.x + first.y * second.y;
}

/// Returns the squared length. @determinism deterministic @since 0.1
[[nodiscard]] constexpr f32 LengthSquared(const Vec2& vector) {
    return Dot(vector, vector);
}

/// Returns the length. @determinism deterministic @since 0.1
[[nodiscard]] inline f32 Length(const Vec2& vector) {
    return Sqrt(LengthSquared(vector));
}

/// Returns the vector scaled to length 1. @pre The vector is not zero. @determinism deterministic @since 0.1
[[nodiscard]] inline Vec2 Normalize(const Vec2& vector) {
    const f32 length = Length(vector);
    NX_ASSERT(length > 0.0f, "Cannot normalize a zero vector.");
    return vector / length;
}

/// Interpolates linearly, component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec2 Lerp(const Vec2& first, const Vec2& second, f32 fraction) {
    return Vec2{Lerp(first.x, second.x, fraction), Lerp(first.y, second.y, fraction)};
}

} // namespace Nexyl::Math
