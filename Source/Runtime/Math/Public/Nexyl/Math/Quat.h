#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Math/Functions.h>
#include <Nexyl/Math/Scalar.h>
#include <Nexyl/Math/Vec3.h>

namespace Nexyl::Math {

/// A rotation as a unit quaternion of f32 (docs/NEXYL_ARCHITECTURE.md, section 1.1).
///
/// Positive angles turn counter-clockwise looking down the axis toward the origin. `a * b` applies `b` first, then
/// `a`. The components are stored x, y, z, w like `NxQuat` of the C ABI; the default is the identity.
///
/// ```cpp
/// const Quat yaw = Quat::FromAxisAngle(Vec3::Up(), kHalfPi<f32>);
/// const Vec3 left = Rotate(yaw, Vec3::Forward()); // (0, 1, 0)
/// ```
/// @threadsafety compatible @determinism deterministic @since 0.1
struct Quat {
    /// The first imaginary component.
    f32 x = 0.0f;
    /// The second imaginary component.
    f32 y = 0.0f;
    /// The third imaginary component.
    f32 z = 0.0f;
    /// The real component.
    f32 w = 1.0f;

    /// The rotation that changes nothing. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Quat Identity() {
        return Quat{};
    }

    /// The rotation by `angle` radians about a unit axis.
    /// @param axis  The axis. @pre Length 1.
    /// @param angle The angle in radians.
    /// @return The rotation.
    /// @determinism deterministic @since 0.1
    [[nodiscard]] static Quat FromAxisAngle(const Vec3& axis, f32 angle) {
        const SineCosine half = SinCos(angle * 0.5f);
        return Quat{axis.x * half.sine, axis.y * half.sine, axis.z * half.sine, half.cosine};
    }

    /// Returns the imaginary part as a vector. @determinism deterministic @since 0.1
    [[nodiscard]] constexpr Vec3 Imaginary() const {
        return Vec3{x, y, z};
    }

    /// Compares exactly, component by component. @since 0.1
    constexpr bool operator==(const Quat&) const = default;
};

static_assert(sizeof(Quat) == 16, "Quat is four packed f32, like NxQuat.");

/// Composes two rotations: the result applies `second` first, then `first` (Hamilton product).
/// @determinism deterministic @since 0.1
[[nodiscard]] constexpr Quat operator*(const Quat& first, const Quat& second) {
    return Quat{
        ((first.w * second.x + first.x * second.w) + first.y * second.z) - first.z * second.y,
        ((first.w * second.y - first.x * second.z) + first.y * second.w) + first.z * second.x,
        ((first.w * second.z + first.x * second.y) - first.y * second.x) + first.z * second.w,
        ((first.w * second.w - first.x * second.x) - first.y * second.y) - first.z * second.z,
    };
}

/// Returns the dot product of the four components. @determinism deterministic @since 0.1
[[nodiscard]] constexpr f32 Dot(const Quat& first, const Quat& second) {
    return ((first.x * second.x + first.y * second.y) + first.z * second.z) + first.w * second.w;
}

/// Returns the conjugate, which is the inverse of a unit quaternion. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Quat Conjugate(const Quat& rotation) {
    return Quat{-rotation.x, -rotation.y, -rotation.z, rotation.w};
}

/// Returns the quaternion scaled to length 1, removing accumulated rounding.
/// @pre The quaternion is not zero. @determinism deterministic @since 0.1
[[nodiscard]] inline Quat Normalize(const Quat& rotation) {
    const f32 length = Sqrt(Dot(rotation, rotation));
    NX_ASSERT(length > 0.0f, "Cannot normalize a zero quaternion.");
    return Quat{rotation.x / length, rotation.y / length, rotation.z / length, rotation.w / length};
}

/// Rotates a vector: v + 2w (u x v) + 2 u x (u x v) with u the imaginary part.
/// @param rotation A unit quaternion.
/// @param vector   The vector to rotate.
/// @return The rotated vector.
/// @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 Rotate(const Quat& rotation, const Vec3& vector) {
    const Vec3 imaginary = rotation.Imaginary();
    const Vec3 twice = Cross(imaginary, vector) * 2.0f;
    return (vector + twice * rotation.w) + Cross(imaginary, twice);
}

/// Interpolates between two rotations along the shorter arc at constant angular speed. Nearly equal rotations
/// interpolate linearly and renormalise, where the arc formula loses precision.
/// @param first    The rotation at fraction 0.
/// @param second   The rotation at fraction 1.
/// @param fraction In [0, 1].
/// @return A unit quaternion.
/// @determinism deterministic @since 0.1
[[nodiscard]] inline Quat Slerp(const Quat& first, const Quat& second, f32 fraction) {
    f32 cosine = Dot(first, second);
    Quat target = second;
    if (cosine < 0.0f) {
        // q and -q are the same rotation; take the one on the shorter arc.
        target = Quat{-second.x, -second.y, -second.z, -second.w};
        cosine = -cosine;
    }
    if (cosine > 0.9995f) {
        return Normalize(Quat{Lerp(first.x, target.x, fraction), Lerp(first.y, target.y, fraction),
                              Lerp(first.z, target.z, fraction), Lerp(first.w, target.w, fraction)});
    }
    const f32 angle = Acos(cosine);
    const f32 sine = Sin(angle);
    const f32 firstWeight = Sin((1.0f - fraction) * angle) / sine;
    const f32 secondWeight = Sin(fraction * angle) / sine;
    return Quat{first.x * firstWeight + target.x * secondWeight, first.y * firstWeight + target.y * secondWeight,
                first.z * firstWeight + target.z * secondWeight, first.w * firstWeight + target.w * secondWeight};
}

} // namespace Nexyl::Math
