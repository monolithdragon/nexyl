#pragma once

#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Math/Mat3.h>
#include <Nexyl/Math/Quat.h>
#include <Nexyl/Math/Vec3.h>
#include <Nexyl/Math/Vec4.h>

namespace Nexyl::Math {

/// A 4x4 matrix of f32 for affine and projective transforms, stored as four columns (decision D-34).
///
/// Vectors are columns and are multiplied on the right: `m * v`, so `a * b` applies `b` first; the translation is
/// the last column. The default is the identity.
///
/// ```cpp
/// const Mat4 model = Mat4::FromTranslationRotationScale(offset, rotation, Vec3{2.0f, 2.0f, 2.0f});
/// const Vec3 corner = TransformPoint(model, Vec3{1.0f, 1.0f, 1.0f});
/// ```
/// @threadsafety compatible @determinism deterministic @since 0.1
struct Mat4 {
    /// The columns; `columns[c]` holds the elements of column c.
    Vec4 columns[4] = {Vec4{1.0f, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, 1.0f, 0.0f, 0.0f}, Vec4{0.0f, 0.0f, 1.0f, 0.0f},
                       Vec4{0.0f, 0.0f, 0.0f, 1.0f}};

    /// The identity. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Mat4 Identity() {
        return Mat4{};
    }

    /// The translation by `offset`. @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Mat4 FromTranslation(const Vec3& offset) {
        Mat4 result;
        result.columns[3] = Vec4::FromVec3(offset, 1.0f);
        return result;
    }

    /// Scales, then rotates, then translates: T * R * S. @param rotation A unit quaternion.
    /// @determinism deterministic @since 0.1
    [[nodiscard]] static constexpr Mat4 FromTranslationRotationScale(const Vec3& offset, const Quat& rotation,
                                                                     const Vec3& scale) {
        const Mat3 basis = Mat3::FromRotation(rotation);
        return Mat4{{Vec4::FromVec3(basis.columns[0] * scale.x, 0.0f), Vec4::FromVec3(basis.columns[1] * scale.y, 0.0f),
                     Vec4::FromVec3(basis.columns[2] * scale.z, 0.0f), Vec4::FromVec3(offset, 1.0f)}};
    }

    /// Compares exactly, element by element. @since 0.1
    constexpr bool operator==(const Mat4&) const = default;
};

/// Transforms a column vector, summing the scaled columns in order. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec4 operator*(const Mat4& matrix, const Vec4& vector) {
    return ((matrix.columns[0] * vector.x + matrix.columns[1] * vector.y) + matrix.columns[2] * vector.z) +
           matrix.columns[3] * vector.w;
}

/// Composes two matrices: the result applies `second` first. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Mat4 operator*(const Mat4& first, const Mat4& second) {
    return Mat4{
        {first * second.columns[0], first * second.columns[1], first * second.columns[2], first * second.columns[3]}};
}

/// Transforms a point (w = 1), without a perspective divide. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 TransformPoint(const Mat4& matrix, const Vec3& point) {
    return (matrix * Vec4::FromVec3(point, 1.0f)).Xyz();
}

/// Transforms a direction (w = 0), which ignores the translation. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 TransformDirection(const Mat4& matrix, const Vec3& direction) {
    return (matrix * Vec4::FromVec3(direction, 0.0f)).Xyz();
}

/// Swaps rows and columns. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Mat4 Transpose(const Mat4& matrix) {
    const Vec4* const c = matrix.columns;
    return Mat4{{Vec4{c[0].x, c[1].x, c[2].x, c[3].x}, Vec4{c[0].y, c[1].y, c[2].y, c[3].y},
                 Vec4{c[0].z, c[1].z, c[2].z, c[3].z}, Vec4{c[0].w, c[1].w, c[2].w, c[3].w}}};
}

/// Returns the inverse, or kNone if the determinant is zero. Uses the expansion by complementary 2x2 minors of the
/// top and bottom row pairs, evaluated in a fixed order. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Option<Mat4> Inverse(const Mat4& matrix) {
    // aRC is row R, column C.
    const Vec4* const c = matrix.columns;
    const f32 a00 = c[0].x, a10 = c[0].y, a20 = c[0].z, a30 = c[0].w;
    const f32 a01 = c[1].x, a11 = c[1].y, a21 = c[1].z, a31 = c[1].w;
    const f32 a02 = c[2].x, a12 = c[2].y, a22 = c[2].z, a32 = c[2].w;
    const f32 a03 = c[3].x, a13 = c[3].y, a23 = c[3].z, a33 = c[3].w;

    // Minors of rows 0 and 1, and of rows 2 and 3.
    const f32 s0 = a00 * a11 - a10 * a01;
    const f32 s1 = a00 * a12 - a10 * a02;
    const f32 s2 = a00 * a13 - a10 * a03;
    const f32 s3 = a01 * a12 - a11 * a02;
    const f32 s4 = a01 * a13 - a11 * a03;
    const f32 s5 = a02 * a13 - a12 * a03;
    const f32 t0 = a20 * a31 - a30 * a21;
    const f32 t1 = a20 * a32 - a30 * a22;
    const f32 t2 = a20 * a33 - a30 * a23;
    const f32 t3 = a21 * a32 - a31 * a22;
    const f32 t4 = a21 * a33 - a31 * a23;
    const f32 t5 = a22 * a33 - a32 * a23;

    const f32 determinant = ((((s0 * t5 - s1 * t4) + s2 * t3) + s3 * t2) - s4 * t1) + s5 * t0;
    if (determinant == 0.0f) {
        return kNone;
    }

    // bRC of the adjugate, divided by the determinant.
    const auto entry = [determinant](f32 value) { return value / determinant; };
    return Mat4{{
        Vec4{entry((a11 * t5 - a12 * t4) + a13 * t3), entry((-a10 * t5 + a12 * t2) - a13 * t1),
             entry((a10 * t4 - a11 * t2) + a13 * t0), entry((-a10 * t3 + a11 * t1) - a12 * t0)},
        Vec4{entry((-a01 * t5 + a02 * t4) - a03 * t3), entry((a00 * t5 - a02 * t2) + a03 * t1),
             entry((-a00 * t4 + a01 * t2) - a03 * t0), entry((a00 * t3 - a01 * t1) + a02 * t0)},
        Vec4{entry((a31 * s5 - a32 * s4) + a33 * s3), entry((-a30 * s5 + a32 * s2) - a33 * s1),
             entry((a30 * s4 - a31 * s2) + a33 * s0), entry((-a30 * s3 + a31 * s1) - a32 * s0)},
        Vec4{entry((-a21 * s5 + a22 * s4) - a23 * s3), entry((a20 * s5 - a22 * s2) + a23 * s1),
             entry((-a20 * s4 + a21 * s2) - a23 * s0), entry((a20 * s3 - a21 * s1) + a22 * s0)},
    }};
}

} // namespace Nexyl::Math
