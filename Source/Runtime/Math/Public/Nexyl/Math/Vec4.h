#pragma once

#include <Nexyl/Base/Types.h>
#include <Nexyl/Math/Scalar.h>
#include <Nexyl/Math/Vec3.h>

namespace Nexyl::Math {

/// A 4D vector of f32: homogeneous coordinates, matrix columns, colours.
/// @threadsafety compatible @determinism deterministic @since 0.1
struct Vec4 {
    /// The first component.
    f32 x = 0.0f;
    /// The second component.
    f32 y = 0.0f;
    /// The third component.
    f32 z = 0.0f;
    /// The fourth component.
    f32 w = 0.0f;

    /// Extends a 3D vector: w = 1 makes a point, w = 0 a direction. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Vec4 FromVec3(const Vec3& vector, f32 w) {
        return Vec4{vector.x, vector.y, vector.z, w};
    }

    /// Returns the first three components. @determinism deterministic @since 0.1
    [[nodiscard]] constexpr Vec3 Xyz() const {
        return Vec3{x, y, z};
    }

    /// Compares exactly, component by component. @since 0.1
    constexpr bool operator==(const Vec4&) const = default;

    /// Negates every component. @determinism deterministic @since 0.1
    [[nodiscard]] constexpr Vec4 operator-() const {
        return Vec4{-x, -y, -z, -w};
    }
};

static_assert(sizeof(Vec4) == 16, "Vec4 is four packed f32.");

/// Adds component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec4 operator+(const Vec4& first, const Vec4& second) {
    return Vec4{first.x + second.x, first.y + second.y, first.z + second.z, first.w + second.w};
}

/// Subtracts component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec4 operator-(const Vec4& first, const Vec4& second) {
    return Vec4{first.x - second.x, first.y - second.y, first.z - second.z, first.w - second.w};
}

/// Scales every component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec4 operator*(const Vec4& vector, f32 factor) {
    return Vec4{vector.x * factor, vector.y * factor, vector.z * factor, vector.w * factor};
}

/// Scales every component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec4 operator*(f32 factor, const Vec4& vector) {
    return vector * factor;
}

/// Divides every component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec4 operator/(const Vec4& vector, f32 divisor) {
    return Vec4{vector.x / divisor, vector.y / divisor, vector.z / divisor, vector.w / divisor};
}

/// Returns the dot product, summed in order x, y, z, w. @determinism deterministic @since 0.1
[[nodiscard]] constexpr f32 Dot(const Vec4& first, const Vec4& second) {
    return ((first.x * second.x + first.y * second.y) + first.z * second.z) + first.w * second.w;
}

/// Interpolates linearly, component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec4 Lerp(const Vec4& first, const Vec4& second, f32 fraction) {
    return Vec4{Lerp(first.x, second.x, fraction), Lerp(first.y, second.y, fraction), Lerp(first.z, second.z, fraction),
                Lerp(first.w, second.w, fraction)};
}

} // namespace Nexyl::Math
