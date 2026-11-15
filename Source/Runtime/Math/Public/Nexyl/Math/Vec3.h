#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Math/Scalar.h>

namespace Nexyl::Math {

/// A 3D vector of f32: directions, velocities, forces and local offsets (docs/NEXYL_ARCHITECTURE.md, section 1.1).
///
/// The axes are right-handed, with X forward, Y left and Z up, and quantities are in SI units. World positions
/// use Vec3d. The layout matches `NxVec3` of the C ABI. Sums of components are taken in the order x, y, z.
///
/// ```cpp
/// const Vec3 velocity = Vec3::Forward() * 3.0f + Vec3::Up();
/// const f32 speed = Length(velocity);
/// ```
/// @threadsafety compatible @determinism deterministic @since 0.1
struct Vec3 {
    /// The forward component.
    f32 x = 0.0f;
    /// The left component.
    f32 y = 0.0f;
    /// The up component.
    f32 z = 0.0f;

    /// The unit vector along +X, forward. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Vec3 Forward() {
        return Vec3{1.0f, 0.0f, 0.0f};
    }

    /// The unit vector along +Y, left. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Vec3 Left() {
        return Vec3{0.0f, 1.0f, 0.0f};
    }

    /// The unit vector along +Z, up. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Vec3 Up() {
        return Vec3{0.0f, 0.0f, 1.0f};
    }

    /// Compares exactly, component by component. @since 0.1
    constexpr bool operator==(const Vec3&) const = default;

    /// Negates every component. @determinism deterministic @since 0.1
    [[nodiscard]] constexpr Vec3 operator-() const {
        return Vec3{-x, -y, -z};
    }

    /// Adds `other` component by component. @determinism deterministic @since 0.1
    constexpr Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    /// Subtracts `other` component by component. @determinism deterministic @since 0.1
    constexpr Vec3& operator-=(const Vec3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    /// Scales every component. @determinism deterministic @since 0.1
    constexpr Vec3& operator*=(f32 factor) {
        x *= factor;
        y *= factor;
        z *= factor;
        return *this;
    }
};

static_assert(sizeof(Vec3) == 12, "Vec3 is three packed f32, like NxVec3.");

/// Adds component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 operator+(const Vec3& first, const Vec3& second) {
    return Vec3{first.x + second.x, first.y + second.y, first.z + second.z};
}

/// Subtracts component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 operator-(const Vec3& first, const Vec3& second) {
    return Vec3{first.x - second.x, first.y - second.y, first.z - second.z};
}

/// Scales every component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 operator*(const Vec3& vector, f32 factor) {
    return Vec3{vector.x * factor, vector.y * factor, vector.z * factor};
}

/// Scales every component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 operator*(f32 factor, const Vec3& vector) {
    return vector * factor;
}

/// Divides every component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 operator/(const Vec3& vector, f32 divisor) {
    return Vec3{vector.x / divisor, vector.y / divisor, vector.z / divisor};
}

/// Multiplies component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 MultiplyComponents(const Vec3& first, const Vec3& second) {
    return Vec3{first.x * second.x, first.y * second.y, first.z * second.z};
}

/// Returns the dot product. @determinism deterministic @since 0.1
[[nodiscard]] constexpr f32 Dot(const Vec3& first, const Vec3& second) {
    return (first.x * second.x + first.y * second.y) + first.z * second.z;
}

/// Returns the cross product: right-handed, so Cross(Forward, Left) is Up. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 Cross(const Vec3& first, const Vec3& second) {
    return Vec3{first.y * second.z - first.z * second.y, first.z * second.x - first.x * second.z,
                first.x * second.y - first.y * second.x};
}

/// Returns the squared length. @determinism deterministic @since 0.1
[[nodiscard]] constexpr f32 LengthSquared(const Vec3& vector) {
    return Dot(vector, vector);
}

/// Returns the length. @determinism deterministic @since 0.1
[[nodiscard]] inline f32 Length(const Vec3& vector) {
    return Sqrt(LengthSquared(vector));
}

/// Returns the distance between two points. @determinism deterministic @since 0.1
[[nodiscard]] inline f32 Distance(const Vec3& first, const Vec3& second) {
    return Length(second - first);
}

/// Returns the vector scaled to length 1. @pre The vector is not zero. @determinism deterministic @since 0.1
[[nodiscard]] inline Vec3 Normalize(const Vec3& vector) {
    const f32 length = Length(vector);
    NX_ASSERT(length > 0.0f, "Cannot normalize a zero vector.");
    return vector / length;
}

/// Returns the vector scaled to length 1, or zero for a zero vector. @determinism deterministic @since 0.1
[[nodiscard]] inline Vec3 NormalizeOrZero(const Vec3& vector) {
    const f32 length = Length(vector);
    return length > 0.0f ? vector / length : Vec3{};
}

/// Interpolates linearly, component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 Lerp(const Vec3& first, const Vec3& second, f32 fraction) {
    return Vec3{Lerp(first.x, second.x, fraction), Lerp(first.y, second.y, fraction),
                Lerp(first.z, second.z, fraction)};
}

/// Returns the smaller of each pair of components. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 Min(const Vec3& first, const Vec3& second) {
    return Vec3{Min(first.x, second.x), Min(first.y, second.y), Min(first.z, second.z)};
}

/// Returns the larger of each pair of components. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 Max(const Vec3& first, const Vec3& second) {
    return Vec3{Max(first.x, second.x), Max(first.y, second.y), Max(first.z, second.z)};
}

/// Returns the absolute value of each component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 Abs(const Vec3& vector) {
    return Vec3{Abs(vector.x), Abs(vector.y), Abs(vector.z)};
}

} // namespace Nexyl::Math
