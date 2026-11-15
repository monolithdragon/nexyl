#include <Nexyl/Base/FloatingPoint.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

NX_TEST(FloatingPoint, SquareRootIsCorrectlyRounded) {
    // The correctly rounded bit patterns of the square root of 2.
    NX_CHECK(BitCast<u32>(SquareRoot(2.0f)) == 0x3FB504F3U);
    NX_CHECK(BitCast<u64>(SquareRoot(2.0)) == 0x3FF6A09E667F3BCDULL);
    NX_CHECK(SquareRoot(16.0f) == 4.0f && SquareRoot(0.25) == 0.5);
    // The smallest subnormal f32, 2^-149, has the root sqrt(2) * 2^-75: exponent field 52, the mantissa of sqrt(2).
    NX_CHECK(BitCast<u32>(SquareRoot(BitCast<f32>(u32{1}))) == 0x1A3504F3U);
}

NX_TEST(FloatingPoint, SquareRootHandlesSpecialValues) {
    const f32 negative = SquareRoot(-1.0f);
    NX_CHECK(negative != negative); // NaN.
    NX_CHECK(BitCast<u32>(SquareRoot(-0.0f)) == 0x80000000U);
    const f64 infinity = BitCast<f64>(u64{0x7FF0000000000000ULL});
    NX_CHECK(SquareRoot(infinity) == infinity);
}
