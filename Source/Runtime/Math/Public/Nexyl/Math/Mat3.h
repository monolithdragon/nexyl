#pragma once

#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Math/Quat.h>
#include <Nexyl/Math/Vec3.h>

namespace Nexyl::Math {

/// A 3x3 matrix of f32 for rotations and scales, stored as three columns (decision D-34).
///
/// Vectors are columns and are multiplied on the right: `m * v`, so `a * b` applies `b` first. The default is the
/// identity.
/// @threadsafety compatible @determinism deterministic @since 0.1
struct Mat3 {
    /// The columns; `columns[c]` holds the elements of column c.
    Vec3 columns[3] = {Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}};

    /// The identity. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Mat3 Identity() {
        return Mat3{};
    }

    /// The scale by `factors` along the axes. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Mat3 FromScale(const Vec3& factors) {
        return Mat3{{Vec3{factors.x, 0.0f, 0.0f}, Vec3{0.0f, factors.y, 0.0f}, Vec3{0.0f, 0.0f, factors.z}}};
    }

    /// The rotation of a unit quaternion. @param rotation A unit quaternion. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Mat3 FromRotation(const Quat& rotation) {
        const f32 xx = rotation.x * rotation.x;
        const f32 yy = rotation.y * rotation.y;
        const f32 zz = rotation.z * rotation.z;
        const f32 xy = rotation.x * rotation.y;
        const f32 xz = rotation.x * rotation.z;
        const f32 yz = rotation.y * rotation.z;
        const f32 wx = rotation.w * rotation.x;
        const f32 wy = rotation.w * rotation.y;
        const f32 wz = rotation.w * rotation.z;
        return Mat3{{
            Vec3{1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy)},
            Vec3{2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx)},
            Vec3{2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy)},
        }};
    }

    /// Compares exactly, element by element. @since 0.1
    constexpr bool operator==(const Mat3&) const = default;
};

/// Transforms a column vector: columns[0] x + columns[1] y + columns[2] z. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 operator*(const Mat3& matrix, const Vec3& vector) {
    return (matrix.columns[0] * vector.x + matrix.columns[1] * vector.y) + matrix.columns[2] * vector.z;
}

/// Composes two matrices: the result applies `second` first. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Mat3 operator*(const Mat3& first, const Mat3& second) {
    return Mat3{{first * second.columns[0], first * second.columns[1], first * second.columns[2]}};
}

/// Swaps rows and columns. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Mat3 Transpose(const Mat3& matrix) {
    const Vec3* const c = matrix.columns;
    return Mat3{{Vec3{c[0].x, c[1].x, c[2].x}, Vec3{c[0].y, c[1].y, c[2].y}, Vec3{c[0].z, c[1].z, c[2].z}}};
}

/// Returns the determinant, c0 . (c1 x c2). @determinism deterministic @since 0.1
[[nodiscard]] constexpr f32 Determinant(const Mat3& matrix) {
    return Dot(matrix.columns[0], Cross(matrix.columns[1], matrix.columns[2]));
}

/// Returns the inverse, or kNone if the determinant is zero. Rows of the inverse are cross products of the
/// columns divided by the determinant. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Option<Mat3> Inverse(const Mat3& matrix) {
    const f32 determinant = Determinant(matrix);
    if (determinant == 0.0f) {
        return kNone;
    }
    const Vec3* const c = matrix.columns;
    const Mat3 rows{
        {Cross(c[1], c[2]) / determinant, Cross(c[2], c[0]) / determinant, Cross(c[0], c[1]) / determinant}};
    return Transpose(rows);
}

} // namespace Nexyl::Math
