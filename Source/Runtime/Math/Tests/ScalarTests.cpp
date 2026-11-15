#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Math/Functions.h>
#include <Nexyl/Math/Scalar.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Math;

// The scalar functions work in constant expressions.
static_assert(Trunc(2.75f) == 2.0f && Trunc(-2.75f) == -2.0f && Trunc(1e30f) == 1e30f);
static_assert(Floor(-2.25f) == -3.0f && Floor(2.25) == 2.0 && Ceil(-2.25f) == -2.0f && Ceil(2.25) == 3.0);
static_assert(Round(2.5f) == 3.0f && Round(-2.5f) == -3.0f && Round(2.4999998f) == 2.0f);
static_assert(Min(1, 2) == 1 && Max(1.0f, 2.0f) == 2.0f && Clamp(5, 0, 3) == 3 && Clamp(-1.0, 0.0, 1.0) == 0.0);
static_assert(Abs(-3.5f) == 3.5f && CopySign(2.0, -0.0) == -2.0);
static_assert(SaturateToI32(1e10f) == 2147483647 && SaturateToI32(-1e10) == -2147483647 - 1);
static_assert(SaturateToI32(-2.9f) == -2 && SaturateToI64(1e30) == 9223372036854775807LL);
static_assert(Lerp(2.0f, 6.0f, 0.25f) == 3.0f);

NX_TEST(Scalar, ClassifiesSpecialValues) {
    const f32 nan = QuietNaN<f32>();
    const f64 infinity = Infinity<f64>();
    NX_CHECK(IsNaN(nan) && !IsNaN(infinity) && !IsNaN(0.0f));
    NX_CHECK(IsInfinite(infinity) && IsInfinite(-infinity) && !IsInfinite(nan));
    NX_CHECK(IsFinite(1e308) && !IsFinite(infinity) && !IsFinite(nan));
    NX_CHECK(BitCast<u32>(Abs(-0.0f)) == 0 && SaturateToI32(nan) == 0 && SaturateToI64(infinity) > 0);
}

NX_TEST(Scalar, RoundsZerosAndHalfwayCasesConsistently) {
    // The sign of zero survives every rounding function.
    NX_CHECK(BitCast<u32>(Trunc(-0.5f)) == 0x8000'0000U);
    NX_CHECK(BitCast<u32>(Ceil(-0.5f)) == 0x8000'0000U);
    NX_CHECK(BitCast<u32>(Round(-0.25f)) == 0x8000'0000U);
    NX_CHECK(Floor(-0.5f) == -1.0f && Round(0.5f) == 1.0f && Round(-0.5f) == -1.0f);
    // Values with no fraction bits are returned unchanged.
    NX_CHECK(Floor(16777217.0) == 16777217.0 && Round(-8388609.0f) == -8388609.0f);
}

NX_TEST(Scalar, MinAndMaxSelectTheFirstOperandForNaNAndZeros) {
    const f32 nan = QuietNaN<f32>();
    NX_CHECK(IsNaN(Min(nan, 1.0f)) && Min(1.0f, nan) == 1.0f);
    NX_CHECK(IsNaN(Max(nan, 1.0f)) && Max(1.0f, nan) == 1.0f);
    NX_CHECK(BitCast<u32>(Min(-0.0f, 0.0f)) == 0x8000'0000U && BitCast<u32>(Max(0.0f, -0.0f)) == 0);
    NX_CHECK(BitCast<u32>(Min(0.0f, -0.0f)) == 0 && BitCast<u32>(Max(-0.0f, 0.0f)) == 0x8000'0000U);
}

NX_TEST(Scalar, ClampKeepsTheValueAtAZeroBoundAndNaN) {
    NX_CHECK(BitCast<u32>(Clamp(-0.0f, 0.0f, 1.0f)) == 0x8000'0000U && BitCast<u32>(Clamp(0.0f, -0.0f, 1.0f)) == 0);
    NX_CHECK(BitCast<u32>(Clamp(0.0f, -1.0f, -0.0f)) == 0 && BitCast<u32>(Clamp(-0.0f, -1.0f, 0.0f)) == 0x8000'0000U);
    NX_CHECK(Clamp(2.0f, 0.0f, 1.0f) == 1.0f && Clamp(-2.0, 0.0, 1.0) == 0.0 && Clamp<i32>(5, 0, 3) == 3);
    NX_CHECK(IsNaN(Clamp(QuietNaN<f32>(), 0.0f, 1.0f)));
}

NX_TEST(Scalar, MinMaxAndClampGiveTheSameBitsWhenFoldedAndWhenRun) {
    // An optimising compiler folds the literal cases above. Sin(0) is +0, computed in another translation unit,
    // so these calls run the generated code; both paths must give the same bits.
    const f32 zero = Sin(0.0f);
    const f32 negativeZero = -zero;
    NX_CHECK(BitCast<u32>(Min(negativeZero, zero)) == 0x8000'0000U && BitCast<u32>(Max(zero, negativeZero)) == 0);
    NX_CHECK(BitCast<u32>(Min(zero, negativeZero)) == 0 && BitCast<u32>(Max(negativeZero, zero)) == 0x8000'0000U);
    NX_CHECK(BitCast<u32>(Clamp(negativeZero, zero, 1.0f)) == 0x8000'0000U &&
             BitCast<u32>(Clamp(zero, -1.0f, negativeZero)) == 0);
}

NX_TEST(Scalar, SqrtIsTheCorrectlyRoundedPrimitive) {
    NX_CHECK(Sqrt(9.0f) == 3.0f && Sqrt(2.25) == 1.5);
    NX_CHECK(BitCast<u32>(Sqrt(2.0f)) == 0x3FB504F3U);
    NX_CHECK(IsNaN(Sqrt(-4.0)));
}
