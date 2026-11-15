#pragma once

#include <Nexyl/Base/Types.h>
#include <Nexyl/Math/Scalar.h>
#include <Nexyl/Math/Vec3.h>

namespace Nexyl::Math {

/// A world position in f64 (docs/NEXYL_ARCHITECTURE.md, sections 1.1 and 7.5; decision D-13).
///
/// Positions are the one f64 quantity; everything relative to a position is a Vec3. The two meet only through
/// ToLocal, which narrows a difference against a deterministic local origin, and Offset, which widens a
/// displacement exactly before adding it. Absolute world coordinates are never truncated to f32. The layout
/// matches `NxVec3d` of the C ABI.
///
/// ```cpp
/// position = Offset(position, velocity * timeStep); // the one form of position integration
/// const Vec3 local = ToLocal(contactPoint, body.centreOfMass);
/// ```
/// @threadsafety compatible @determinism deterministic @since 0.1
struct Vec3d {
    /// The forward component, in metres.
    f64 x = 0.0;
    /// The left component, in metres.
    f64 y = 0.0;
    /// The up component, in metres.
    f64 z = 0.0;

    /// Widens a Vec3 exactly. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Vec3d FromVec3(const Vec3& vector) {
        return Vec3d{static_cast<f64>(vector.x), static_cast<f64>(vector.y), static_cast<f64>(vector.z)};
    }

    /// Compares exactly, component by component. @since 0.1
    constexpr bool operator==(const Vec3d&) const = default;

    /// Negates every component. @determinism deterministic @since 0.1
    [[nodiscard]] constexpr Vec3d operator-() const {
        return Vec3d{-x, -y, -z};
    }
};

static_assert(sizeof(Vec3d) == 24, "Vec3d is three packed f64, like NxVec3d.");

/// Adds component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3d operator+(const Vec3d& first, const Vec3d& second) {
    return Vec3d{first.x + second.x, first.y + second.y, first.z + second.z};
}

/// Subtracts component by component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3d operator-(const Vec3d& first, const Vec3d& second) {
    return Vec3d{first.x - second.x, first.y - second.y, first.z - second.z};
}

/// Scales every component. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3d operator*(const Vec3d& vector, f64 factor) {
    return Vec3d{vector.x * factor, vector.y * factor, vector.z * factor};
}

/// Returns the dot product, summed in order x, y, z. @determinism deterministic @since 0.1
[[nodiscard]] constexpr f64 Dot(const Vec3d& first, const Vec3d& second) {
    return (first.x * second.x + first.y * second.y) + first.z * second.z;
}

/// Returns the length. @determinism deterministic @since 0.1
[[nodiscard]] inline f64 Length(const Vec3d& vector) {
    return Sqrt(Dot(vector, vector));
}

/// Returns `position` relative to `origin`, narrowed to f32 after the f64 subtraction (rule of section 7.5).
/// @param position A world position.
/// @param origin   A deterministic local origin, such as a centre of mass or the camera.
/// @return The offset of `position` from `origin`.
/// @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 ToLocal(const Vec3d& position, const Vec3d& origin) {
    return Vec3{static_cast<f32>(position.x - origin.x), static_cast<f32>(position.y - origin.y),
                static_cast<f32>(position.z - origin.z)};
}

/// Moves a position by an f32 displacement, widened exactly first (the integration form of section 7.5).
/// @param position     A world position.
/// @param displacement The change, for example velocity times time step, computed and rounded in f32.
/// @return The moved position.
/// @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3d Offset(const Vec3d& position, const Vec3& displacement) {
    return position + Vec3d::FromVec3(displacement);
}

} // namespace Nexyl::Math
