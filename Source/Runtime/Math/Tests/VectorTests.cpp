#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Math/Functions.h>
#include <Nexyl/Math/Mat3.h>
#include <Nexyl/Math/Mat4.h>
#include <Nexyl/Math/Pose.h>
#include <Nexyl/Math/Quat.h>
#include <Nexyl/Math/Scalar.h>
#include <Nexyl/Math/Vec2.h>
#include <Nexyl/Math/Vec3.h>
#include <Nexyl/Math/Vec3d.h>
#include <Nexyl/Math/Vec4.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Math;

// The axis conventions of section 1.1, in constant expressions: right-handed, X forward, Y left, Z up.
static_assert(Cross(Vec3::Forward(), Vec3::Left()) == Vec3::Up());
static_assert(Cross(Vec3::Left(), Vec3::Up()) == Vec3::Forward());
static_assert(Dot(Vec3{1.0f, 2.0f, 3.0f}, Vec3{4.0f, 5.0f, 6.0f}) == 32.0f);
static_assert(Vec4::FromVec3(Vec3{1.0f, 2.0f, 3.0f}, 1.0f).Xyz() == Vec3{1.0f, 2.0f, 3.0f});
static_assert(Transpose(Transpose(Mat4::FromTranslation(Vec3{1.0f, 2.0f, 3.0f}))) ==
              Mat4::FromTranslation(Vec3{1.0f, 2.0f, 3.0f}));

namespace {

bool Near(f32 first, f32 second, f32 tolerance = 1e-6f) {
    return Abs(first - second) <= tolerance;
}

bool Near(const Vec3& first, const Vec3& second, f32 tolerance = 1e-6f) {
    return Near(first.x, second.x, tolerance) && Near(first.y, second.y, tolerance) &&
           Near(first.z, second.z, tolerance);
}

bool IsNearIdentity(const Mat4& matrix) {
    const Mat4 identity;
    for (u32 column = 0; column < 4; ++column) {
        const Vec4 difference = matrix.columns[column] - identity.columns[column];
        if (!Near(difference.x, 0.0f, 1e-5f) || !Near(difference.y, 0.0f, 1e-5f) || !Near(difference.z, 0.0f, 1e-5f) ||
            !Near(difference.w, 0.0f, 1e-5f)) {
            return false;
        }
    }
    return true;
}

} // namespace

NX_TEST(Vec, ArithmeticAndLength) {
    Vec3 value{3.0f, 4.0f, 0.0f};
    NX_CHECK((Length(value) == 5.0f && LengthSquared(value) == 25.0f));
    NX_CHECK((Normalize(value) == Vec3{0.6f, 0.8f, 0.0f}));
    NX_CHECK((NormalizeOrZero(Vec3{}) == Vec3{}));
    value += Vec3{1.0f, 1.0f, 1.0f};
    value *= 2.0f;
    NX_CHECK((value == Vec3{8.0f, 10.0f, 2.0f} && -value == Vec3{-8.0f, -10.0f, -2.0f}));
    NX_CHECK((Min(value, Vec3{9.0f, 9.0f, 9.0f}) == Vec3{8.0f, 9.0f, 2.0f}));
    NX_CHECK((Distance(Vec3{}, Vec3{0.0f, 0.0f, 2.0f}) == 2.0f));
    NX_CHECK((Lerp(Vec2{0.0f, 2.0f}, Vec2{4.0f, 6.0f}, 0.5f) == Vec2{2.0f, 4.0f}));
    NX_CHECK((Length(Vec2{6.0f, 8.0f}) == 10.0f &&
              Dot(Vec4{1.0f, 1.0f, 1.0f, 1.0f}, Vec4{1.0f, 2.0f, 3.0f, 4.0f}) == 10.0f));
}

NX_TEST(Vec3d, KeepsWorldPrecisionAndNarrowsLocally) {
    // At 10 000 km a millimetre (2^-10 m) survives exactly in f64 and in the local f32 difference; in f32 world
    // coordinates the resolution there would be one metre.
    const Vec3d origin{1e7, -1e7, 1e7};
    const Vec3d position = Offset(origin, Vec3{0.0009765625f, 1.5f, -2.0f});
    NX_CHECK((ToLocal(position, origin) == Vec3{0.0009765625f, 1.5f, -2.0f}));
    NX_CHECK((position.x == 1e7 + 0.0009765625));
    NX_CHECK((Length(Vec3d{3.0, 0.0, 4.0}) == 5.0));
}

NX_TEST(Quat, RotatesCounterClockwiseAboutTheAxis) {
    const Quat quarter = Quat::FromAxisAngle(Vec3::Up(), kHalfPi<f32>);
    NX_CHECK((Near(Rotate(quarter, Vec3::Forward()), Vec3::Left())));
    NX_CHECK((Near(Rotate(quarter * quarter, Vec3::Forward()), -Vec3::Forward())));
    NX_CHECK(
        (Near(Rotate(Conjugate(quarter), Rotate(quarter, Vec3{1.0f, 2.0f, 3.0f})), Vec3{1.0f, 2.0f, 3.0f}, 1e-5f)));
    NX_CHECK((Rotate(Quat::Identity(), Vec3{1.0f, 2.0f, 3.0f}) == Vec3{1.0f, 2.0f, 3.0f}));

    // The product applies its right operand first: yaw after pitch.
    const Quat pitch = Quat::FromAxisAngle(Vec3::Left(), kHalfPi<f32>);
    NX_CHECK((Near(Rotate(quarter * pitch, Vec3::Forward()), Rotate(quarter, Rotate(pitch, Vec3::Forward())))));

    const Quat half = Slerp(Quat::Identity(), quarter, 0.5f);
    const Quat eighth = Quat::FromAxisAngle(Vec3::Up(), kQuarterPi<f32>);
    NX_CHECK((Near(Dot(half, eighth), 1.0f, 1e-6f)));
    NX_CHECK((Near(Dot(Normalize(Quat{0.0f, 0.0f, 2.0f, 0.0f}), Quat{0.0f, 0.0f, 1.0f, 0.0f}), 1.0f)));
}

NX_TEST(Mat, AgreesWithQuaternionsAndInverts) {
    const Quat rotation = Normalize(Quat::FromAxisAngle(Normalize(Vec3{1.0f, 2.0f, 3.0f}), 0.7f));
    const Vec3 vector{0.5f, -1.5f, 2.0f};
    NX_CHECK((Near(Mat3::FromRotation(rotation) * vector, Rotate(rotation, vector), 1e-5f)));
    NX_CHECK((Near(Determinant(Mat3::FromRotation(rotation)), 1.0f, 1e-5f)));

    const Option<Mat3> inverse3 = Inverse(Mat3::FromScale(Vec3{2.0f, 4.0f, 8.0f}));
    NX_REQUIRE(inverse3.HasValue());
    NX_CHECK((inverse3.GetValue() == Mat3::FromScale(Vec3{0.5f, 0.25f, 0.125f})));
    NX_CHECK((!Inverse(Mat3::FromScale(Vec3{1.0f, 0.0f, 1.0f})).HasValue()));

    const Mat4 model = Mat4::FromTranslationRotationScale(Vec3{10.0f, -5.0f, 2.0f}, rotation, Vec3{2.0f, 3.0f, 4.0f});
    const Vec3 expected =
        Rotate(rotation, MultiplyComponents(vector, Vec3{2.0f, 3.0f, 4.0f})) + Vec3{10.0f, -5.0f, 2.0f};
    NX_CHECK((Near(TransformPoint(model, vector), expected, 1e-5f)));
    NX_CHECK((Near(TransformDirection(Mat4::FromTranslation(Vec3{5.0f, 5.0f, 5.0f}), vector), vector)));

    const Option<Mat4> inverse4 = Inverse(model);
    NX_REQUIRE(inverse4.HasValue());
    NX_CHECK((IsNearIdentity(model * inverse4.GetValue()) && IsNearIdentity(inverse4.GetValue() * model)));
    NX_CHECK((!Inverse(Mat4{{Vec4{}, Vec4{}, Vec4{}, Vec4{}}}).HasValue()));
}

NX_TEST(Pose, TransformsBetweenLocalAndWorld) {
    const Pose pose{Vec3d{1e6, 2e6, 3.0}, Quat::FromAxisAngle(Vec3::Up(), kHalfPi<f32>)};
    const Vec3d world = TransformPoint(pose, Vec3::Forward());
    NX_CHECK((Near(ToLocal(world, pose.position), Vec3::Left())));
    NX_CHECK((Near(InverseTransformPoint(pose, world), Vec3::Forward())));
    NX_CHECK((Near(TransformDirection(pose, Vec3::Left()), -Vec3::Forward())));
}
