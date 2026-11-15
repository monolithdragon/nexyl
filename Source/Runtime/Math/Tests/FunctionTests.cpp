#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Math/Functions.h>
#include <Nexyl/Math/Scalar.h>
#include <Nexyl/Test/Test.h>

#include "MathReferences.h"

using namespace Nexyl;
using namespace Nexyl::Math;
using namespace Nexyl::Math::Tests;

namespace {

// The fingerprint of ProduceTheSameBitsOnEveryPlatform, recorded from the first verified run.
constexpr u64 kFunctionFingerprint = 0x13D3'7A08'4AAA'9EE8ULL;

// Distance in units in the last place between two f32 bit patterns of the same sign, or a large value otherwise.
u32 UlpDistance(u32 first, u32 second) {
    const bool firstNaN = IsNaN(BitCast<f32>(first));
    const bool secondNaN = IsNaN(BitCast<f32>(second));
    if (firstNaN || secondNaN) {
        return firstNaN && secondNaN ? 0 : 0xFFFF'FFFFU;
    }
    // Map the sign-magnitude patterns onto one ordered integer line.
    const auto ordered = [](u32 bits) -> i64 {
        return (bits >> 31U) != 0 ? -static_cast<i64>(bits & 0x7FFF'FFFFU) : static_cast<i64>(bits);
    };
    const i64 difference = ordered(first) - ordered(second);
    return static_cast<u32>(difference < 0 ? -difference : difference);
}

struct Accuracy {
    u32 worst = 0;
    usize exact = 0;
};

Accuracy CheckUnary(const UnaryReference* references, usize count, f32 (*function)(f32)) {
    Accuracy accuracy;
    for (usize index = 0; index < count; ++index) {
        const u32 actual = BitCast<u32>(function(BitCast<f32>(references[index].input)));
        const u32 distance = UlpDistance(actual, references[index].expected);
        accuracy.worst = distance > accuracy.worst ? distance : accuracy.worst;
        accuracy.exact += distance == 0 ? 1 : 0;
    }
    return accuracy;
}

Accuracy CheckBinary(const BinaryReference* references, usize count, f32 (*function)(f32, f32)) {
    Accuracy accuracy;
    for (usize index = 0; index < count; ++index) {
        const BinaryReference& reference = references[index];
        const u32 actual = BitCast<u32>(function(BitCast<f32>(reference.first), BitCast<f32>(reference.second)));
        const u32 distance = UlpDistance(actual, reference.expected);
        accuracy.worst = distance > accuracy.worst ? distance : accuracy.worst;
        accuracy.exact += distance == 0 ? 1 : 0;
    }
    return accuracy;
}

// Within one unit in the last place everywhere, and correctly rounded in at least 99% of the cases.
bool MeetsTheBound(const Accuracy& accuracy, usize count) {
    return accuracy.worst <= 1 && accuracy.exact * 100 >= count * 99;
}

f32 SinCosSine(f32 angle) {
    return SinCos(angle).sine;
}

f32 SinCosCosine(f32 angle) {
    return SinCos(angle).cosine;
}

} // namespace

NX_TEST(Functions, TrigonometryMatchesTheReferences) {
    NX_CHECK(MeetsTheBound(CheckUnary(kSinReferences, kSinReferenceCount, &Sin), kSinReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kCosReferences, kCosReferenceCount, &Cos), kCosReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kTanReferences, kTanReferenceCount, &Tan), kTanReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kSinReferences, kSinReferenceCount, &SinCosSine), kSinReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kCosReferences, kCosReferenceCount, &SinCosCosine), kCosReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kAsinReferences, kAsinReferenceCount, &Asin), kAsinReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kAcosReferences, kAcosReferenceCount, &Acos), kAcosReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kAtanReferences, kAtanReferenceCount, &Atan), kAtanReferenceCount));
    NX_CHECK(MeetsTheBound(CheckBinary(kAtan2References, kAtan2ReferenceCount, &Atan2), kAtan2ReferenceCount));
}

NX_TEST(Functions, ExponentialsMatchTheReferences) {
    NX_CHECK(MeetsTheBound(CheckUnary(kExpReferences, kExpReferenceCount, &Exp), kExpReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kExp2References, kExp2ReferenceCount, &Exp2), kExp2ReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kLogReferences, kLogReferenceCount, &Log), kLogReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kLog2References, kLog2ReferenceCount, &Log2), kLog2ReferenceCount));
    NX_CHECK(MeetsTheBound(CheckBinary(kPowReferences, kPowReferenceCount, &Pow), kPowReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kCbrtReferences, kCbrtReferenceCount, &Cbrt), kCbrtReferenceCount));
    NX_CHECK(MeetsTheBound(CheckUnary(kTanhReferences, kTanhReferenceCount, &Tanh), kTanhReferenceCount));
}

NX_TEST(Functions, HandleSpecialValues) {
    const f32 nan = QuietNaN<f32>();
    const f32 infinity = Infinity<f32>();
    NX_CHECK(IsNaN(Sin(infinity)) && IsNaN(Cos(nan)) && IsNaN(Tan(-infinity)));
    NX_CHECK(IsNaN(Asin(1.5f)) && IsNaN(Acos(-1.5f)) && IsNaN(Log(-1.0f)) && IsNaN(Pow(-2.0f, 0.5f)));
    NX_CHECK(BitCast<u32>(Sin(-0.0f)) == 0x8000'0000U && BitCast<u32>(Tanh(-0.0f)) == 0x8000'0000U);
    NX_CHECK(Log(0.0f) == -infinity && Log(infinity) == infinity && Exp(-infinity) == 0.0f);
    NX_CHECK(Exp(100.0f) == infinity && Exp2(128.0f) == infinity && Exp2(-151.0f) == 0.0f);
    NX_CHECK(Exp2(10.0f) == 1024.0f && Log2(1024.0f) == 10.0f && Cbrt(-27.0f) == -3.0f);

    // IEEE-754 atan2 and pow.
    NX_CHECK(Atan2(0.0f, -0.0f) == kPi<f32> && BitCast<u32>(Atan2(-0.0f, 0.0f)) == 0x8000'0000U);
    NX_CHECK(Atan2(1.0f, 0.0f) == kHalfPi<f32> && Atan2(-infinity, infinity) == -kQuarterPi<f32>);
    NX_CHECK(Pow(nan, 0.0f) == 1.0f && Pow(1.0f, nan) == 1.0f && Pow(-1.0f, infinity) == 1.0f);
    NX_CHECK(Pow(0.0f, -3.0f) == infinity && BitCast<u32>(Pow(-0.0f, 3.0f)) == 0x8000'0000U);
    NX_CHECK(Pow(-2.0f, 3.0f) == -8.0f && Pow(-infinity, 3.0f) == -infinity && Pow(0.5f, infinity) == 0.0f);
}

NX_TEST(Functions, ProduceTheSameBitsOnEveryPlatform) {
    // A fingerprint of 2^16 results per function over the whole f32 range. Every supported compiler and CPU must
    // produce exactly these bits; the expected value comes from the first verified run (decision D-35).
    u64 fingerprint = 0xCBF2'9CE4'8422'2325ULL;
    const auto mix = [&fingerprint](f32 value) {
        fingerprint ^= BitCast<u32>(value);
        fingerprint *= 0x0000'0100'0000'01B3ULL;
    };
    for (u32 step = 0; step < 65536; ++step) {
        const f32 any = BitCast<f32>(step * 65537U);
        const f32 small = static_cast<f32>(static_cast<i32>(step) - 32768) / 1024.0f;
        mix(Sin(any));
        mix(Cos(small));
        mix(Tan(small));
        mix(Atan(any));
        mix(Atan2(any, small));
        mix(Asin(small / 32.0f));
        mix(Acos(small / 32.0f));
        mix(Exp(small * 2.5f));
        mix(Exp2(small * 4.0f));
        mix(Log(any));
        mix(Log2(any));
        mix(Pow(small, small / 8.0f));
        mix(Cbrt(any));
        mix(Tanh(small / 2.0f));
    }
    NX_CHECK(fingerprint == kFunctionFingerprint);
}
