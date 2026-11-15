#include <Nexyl/Math/Functions.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Math/Scalar.h>

#include "MathTables.h"

// Every f32 function widens its argument exactly to f64, evaluates there with a relative error of a few 2^-53,
// and rounds once to f32. That error is far below half an f32 unit in the last place, so the result is the
// correctly rounded one unless the exact value lies extremely close to a rounding boundary, and it is never off by
// more than one unit. Only + - * /, the square root and comparisons are used, in a fixed order; the compiler
// configuration forbids contraction into fused multiply-adds (section 7.3), so every platform computes the same
// bits. The series are truncated Taylor series whose first omitted term is below 2^-60 relative to the result.

namespace Nexyl::Math {

namespace {

constexpr f64 kPi64 = 0x1.921fb54442d18p+1;
constexpr f64 kHalfPi64 = 0x1.921fb54442d18p+0;
constexpr f64 kQuarterPi64 = 0x1.921fb54442d18p-1;
constexpr f64 kSixthPi64 = 0x1.0c152382d7366p-1;
constexpr f64 kSqrt2 = 0x1.6a09e667f3bcdp+0;
constexpr f64 kSqrt3 = 0x1.bb67ae8584caap+0;
constexpr f64 kTanTwelfthPi = 0x1.126145e9ecd56p-2;
constexpr f64 kLn2 = 0x1.62e42fefa39efp-1;
constexpr f64 kInvLn2 = 0x1.71547652b82fep+0;
// ln 2 split into a head with 32 significant bits, so that k * head is exact for |k| < 2^21, and the rest.
constexpr f64 kLn2Head = 0x1.62e42fee00000p-1;
constexpr f64 kLn2Tail = 0x1.a39ef35793c76p-33;

// ---- Helpers -----------------------------------------------------------------------------------------------

// 1 / n!, evaluated by the compiler: n! is exact in f64 up to 18!, and the rounding of larger factorials changes
// a coefficient by a few 2^-53, which does not matter for terms this small.
constexpr f64 InverseFactorial(u32 n) {
    f64 factorial = 1.0;
    for (u32 index = 2; index <= n; ++index) {
        factorial *= static_cast<f64>(index);
    }
    return 1.0 / factorial;
}

// Evaluates a polynomial with coefficients from the highest degree down, by Horner's rule.
template <usize N>
f64 Horner(const f64 (&coefficients)[N], f64 argument) {
    f64 sum = coefficients[0];
    for (usize index = 1; index < N; ++index) {
        sum = sum * argument + coefficients[index];
    }
    return sum;
}

// Rounds to f32 with IEEE-754 overflow. Converting an out-of-range double to float is undefined behaviour.
f32 RoundToF32(f64 value) {
    constexpr f64 kOverflowMidpoint = 0x1.ffffffp+127; // Halfway between the largest f32 and 2^128.
    if (value >= kOverflowMidpoint) {
        return Infinity<f32>();
    }
    if (value <= -kOverflowMidpoint) {
        return -Infinity<f32>();
    }
    return static_cast<f32>(value);
}

// Multiplies by 2^exponent, in steps that keep each power of two a normal f64.
f64 ScaleByPowerOfTwo(f64 value, i32 exponent) {
    while (exponent > 1023) {
        value *= 0x1p+1023;
        exponent -= 1023;
    }
    while (exponent < -1022) {
        value *= 0x1p-1022;
        exponent += 1022;
    }
    return value * BitCast<f64>(static_cast<u64>(exponent + 1023) << 52U);
}

// ---- Exponential and logarithm in f64 -----------------------------------------------------------------------

// exp(r) for |r| <= ln(2) / 2: the Taylor series to r^14.
f64 ExpSeries(f64 r) {
    constexpr f64 kTerms[] = {
        InverseFactorial(14),
        InverseFactorial(13),
        InverseFactorial(12),
        InverseFactorial(11),
        InverseFactorial(10),
        InverseFactorial(9),
        InverseFactorial(8),
        InverseFactorial(7),
        InverseFactorial(6),
        InverseFactorial(5),
        InverseFactorial(4),
        InverseFactorial(3),
        InverseFactorial(2),
        1.0,
        1.0,
    };
    return Horner(kTerms, r);
}

// exp(z) - 1 for |z| <= 1.1, without the cancellation of subtracting 1: z times the Taylor series of
// (exp(z) - 1) / z to z^24.
f64 Expm1Series(f64 z) {
    constexpr f64 kTerms[] = {
        InverseFactorial(25), InverseFactorial(24), InverseFactorial(23), InverseFactorial(22), InverseFactorial(21),
        InverseFactorial(20), InverseFactorial(19), InverseFactorial(18), InverseFactorial(17), InverseFactorial(16),
        InverseFactorial(15), InverseFactorial(14), InverseFactorial(13), InverseFactorial(12), InverseFactorial(11),
        InverseFactorial(10), InverseFactorial(9),  InverseFactorial(8),  InverseFactorial(7),  InverseFactorial(6),
        InverseFactorial(5),  InverseFactorial(4),  InverseFactorial(3),  InverseFactorial(2),  1.0,
    };
    return z * Horner(kTerms, z);
}

// e^value for any f64. Results beyond the f64 range become infinity or zero; the f32 functions need far less.
f64 Exp64(f64 value) {
    if (value > 709.0) {
        return Infinity<f64>();
    }
    if (value < -745.0) {
        return 0.0;
    }
    // value = k ln 2 + r with |r| <= ln(2) / 2. k * head is exact and close to value, so the first subtraction is
    // exact as well.
    const f64 k = Round(value * kInvLn2);
    const f64 r = (value - k * kLn2Head) - k * kLn2Tail;
    return ScaleByPowerOfTwo(ExpSeries(r), static_cast<i32>(k));
}

// log(value) split into its exponent and the logarithm of a mantissa in [sqrt(1/2), sqrt(2)).
struct LogParts {
    f64 exponent = 0.0;
    f64 logMantissa = 0.0;
};

// value is positive, finite and normal.
LogParts SplitLog(f64 value) {
    const u64 bits = BitCast<u64>(value);
    i32 exponent = static_cast<i32>(bits >> 52U) - 1023;
    f64 mantissa = BitCast<f64>((bits & 0x000F'FFFF'FFFF'FFFFULL) | 0x3FF0'0000'0000'0000ULL);
    if (mantissa > kSqrt2) {
        mantissa *= 0.5;
        ++exponent;
    }
    // log(m) = 2 atanh(s) with s = (m - 1) / (m + 1), |s| <= 0.1716: the series 2s (1 + s^2/3 + s^4/5 + ...) to
    // s^19. m - 1 is exact for m in [1/2, 2].
    const f64 s = (mantissa - 1.0) / (mantissa + 1.0);
    constexpr f64 kTerms[] = {
        1.0 / 19.0, 1.0 / 17.0, 1.0 / 15.0, 1.0 / 13.0, 1.0 / 11.0, 1.0 / 9.0, 1.0 / 7.0, 1.0 / 5.0, 1.0 / 3.0, 1.0,
    };
    return LogParts{static_cast<f64>(exponent), 2.0 * s * Horner(kTerms, s * s)};
}

f64 Log64(f64 value) {
    const LogParts parts = SplitLog(value);
    return parts.exponent * kLn2 + parts.logMantissa;
}

// ---- Arctangent in f64 -------------------------------------------------------------------------------------

// atan(value) for any f64 except zero, which callers return themselves to keep its sign.
f64 Atan64(f64 value) {
    f64 t = Abs(value);
    bool complement = false;
    if (t > 1.0) {
        // atan(t) = pi/2 - atan(1/t).
        t = 1.0 / t;
        complement = true;
    }
    f64 offset = 0.0;
    if (t > kTanTwelfthPi) {
        // atan(t) = pi/6 + atan((sqrt(3) t - 1) / (sqrt(3) + t)), which brings t into [-tan(pi/12), tan(pi/12)].
        t = (t * kSqrt3 - 1.0) / (t + kSqrt3);
        offset = kSixthPi64;
    }
    // atan(u) = u - u^3/3 + u^5/5 - ... to u^25.
    constexpr f64 kTerms[] = {
        1.0 / 25.0,  -1.0 / 23.0, 1.0 / 21.0, -1.0 / 19.0, 1.0 / 17.0, -1.0 / 15.0, 1.0 / 13.0,
        -1.0 / 11.0, 1.0 / 9.0,   -1.0 / 7.0, 1.0 / 5.0,   -1.0 / 3.0, 1.0,
    };
    f64 result = offset + t * Horner(kTerms, t * t);
    if (complement) {
        result = kHalfPi64 - result;
    }
    return value < 0.0 ? -result : result;
}

// ---- Trigonometric functions in f64 ------------------------------------------------------------------------

// sin(r) for |r| <= pi/4: r + r^3 (-1/3! + r^2/5! - ...) to r^17.
f64 SinSeries(f64 r) {
    constexpr f64 kTerms[] = {
        InverseFactorial(17), -InverseFactorial(15), InverseFactorial(13), -InverseFactorial(11),
        InverseFactorial(9),  -InverseFactorial(7),  InverseFactorial(5),  -InverseFactorial(3),
    };
    const f64 z = r * r;
    return r + r * z * Horner(kTerms, z);
}

// cos(r) for |r| <= pi/4: 1 + r^2 (-1/2! + r^2/4! - ...) to r^16.
f64 CosSeries(f64 r) {
    constexpr f64 kTerms[] = {
        InverseFactorial(16), -InverseFactorial(14), InverseFactorial(12), -InverseFactorial(10),
        InverseFactorial(8),  -InverseFactorial(6),  InverseFactorial(4),  -InverseFactorial(2),
    };
    const f64 z = r * r;
    return 1.0 + z * Horner(kTerms, z);
}

// An angle as k * pi/2 + remainder, with |remainder| <= pi/4 and the quadrant k mod 4.
struct Reduced {
    f64 remainder = 0.0;
    u32 quadrant = 0;
};

// Bits b(start) .. b(start + 63) of the binary fraction of 2/pi, b(start) in the top position; start >= 1.
u64 TwoOverPiWord(i32 start) {
    const auto offset = static_cast<u32>(start - 1);
    const u32 limb = offset / 64U;
    const u32 shift = offset % 64U;
    NX_ASSERT(limb + 1 < Detail::kTwoOverPiLimbCount, "The table of 2/pi is too short for this argument.");
    const u64 high = Detail::kTwoOverPiBits[limb] << shift;
    const u64 low = shift == 0 ? 0 : Detail::kTwoOverPiBits[limb + 1] >> (64U - shift);
    return high | low;
}

// 64 bits of a 256-bit number, stored as four words from the least significant, starting at bit `offset`.
u64 BitsOf(const u64 (&words)[4], u32 offset) {
    const u32 word = offset / 64U;
    const u32 shift = offset % 64U;
    const u64 low = word < 4 ? words[word] >> shift : 0;
    const u64 high = shift != 0 && word + 1 < 4 ? words[word + 1] << (64U - shift) : 0;
    return low | high;
}

// Reduces a finite magnitude of at least pi/4 exactly, by multiplying its integer mantissa with the bits of 2/pi
// that matter: those above bit 2 of the product only add multiples of 4 (whole turns), and those far below it
// cannot reach the 64 fraction bits kept. This works for every f32, including the largest.
Reduced ReduceQuadrant(f32 magnitude) {
    const u32 bits = BitCast<u32>(magnitude);
    const auto mantissa = static_cast<u64>((bits & 0x007F'FFFFU) | 0x0080'0000U);
    const i32 exponent = static_cast<i32>(bits >> 23U) - 127 - 23; // magnitude = mantissa * 2^exponent
    const i32 start = exponent - 1 > 1 ? exponent - 1 : 1;

    // The 192-bit window of 2/pi from bit `start`, times the 24-bit mantissa.
    const u64 window[3] = {TwoOverPiWord(start + 128), TwoOverPiWord(start + 64), TwoOverPiWord(start)};
    u64 product[4] = {};
    u64 carry = 0;
    for (u32 index = 0; index < 3; ++index) {
        const u64 low = mantissa * window[index];
        const u64 sum = low + carry;
        product[index] = sum;
        carry = MultiplyHigh(mantissa, window[index]) + (sum < low ? 1U : 0U);
    }
    product[3] = carry;

    // magnitude * 2/pi = product * 2^-shift.
    const auto shift = static_cast<u32>(start + 191 - exponent);
    u32 quadrant = static_cast<u32>(BitsOf(product, shift)) & 3U;
    const u64 fraction = BitsOf(product, shift - 64U);
    if ((fraction >> 63U) != 0) {
        // Round to the nearest multiple of pi/2; the remainder becomes negative.
        ++quadrant;
    }
    const f64 remainder = static_cast<f64>(BitCast<i64>(fraction)) * 0x1p-64 * kHalfPi64;
    return Reduced{remainder, quadrant & 3U};
}

// Reduces the magnitude of a finite f32 angle; callers restore the sign by symmetry.
Reduced Reduce(f32 angle) {
    const f32 magnitude = Abs(angle);
    if (static_cast<f64>(magnitude) <= kQuarterPi64) {
        return Reduced{static_cast<f64>(magnitude), 0};
    }
    return ReduceQuadrant(magnitude);
}

struct SineCosine64 {
    f64 sine = 0.0;
    f64 cosine = 1.0;
};

// sin and cos of a reduced magnitude, by quadrant.
SineCosine64 OfQuadrant(const Reduced& reduced) {
    const f64 sine = SinSeries(reduced.remainder);
    const f64 cosine = CosSeries(reduced.remainder);
    switch (reduced.quadrant) {
        case 0:
            return SineCosine64{sine, cosine};
        case 1:
            return SineCosine64{cosine, -sine};
        case 2:
            return SineCosine64{-sine, -cosine};
        default:
            return SineCosine64{-cosine, sine};
    }
}

bool IsInteger(f32 value) {
    return IsFinite(value) && Trunc(value) == value;
}

bool IsOddInteger(f32 value) {
    // Every f32 of magnitude 2^24 or more is an even integer.
    return IsInteger(value) && Abs(value) < 16777216.0f && (static_cast<u32>(SaturateToI32(value)) & 1U) != 0;
}

} // namespace

f32 Sin(f32 angle) {
    if (!IsFinite(angle)) {
        return QuietNaN<f32>();
    }
    if (angle == 0.0f) {
        return angle; // Keeps the sign of zero.
    }
    const f64 sine = OfQuadrant(Reduce(angle)).sine;
    return RoundToF32(angle < 0.0f ? -sine : sine);
}

f32 Cos(f32 angle) {
    if (!IsFinite(angle)) {
        return QuietNaN<f32>();
    }
    return RoundToF32(OfQuadrant(Reduce(angle)).cosine);
}

SineCosine SinCos(f32 angle) {
    if (!IsFinite(angle)) {
        return SineCosine{QuietNaN<f32>(), QuietNaN<f32>()};
    }
    const SineCosine64 both = OfQuadrant(Reduce(angle));
    const f32 sine = angle == 0.0f ? angle : RoundToF32(angle < 0.0f ? -both.sine : both.sine);
    return SineCosine{sine, RoundToF32(both.cosine)};
}

f32 Tan(f32 angle) {
    if (!IsFinite(angle)) {
        return QuietNaN<f32>();
    }
    if (angle == 0.0f) {
        return angle;
    }
    const SineCosine64 both = OfQuadrant(Reduce(angle));
    const f64 tangent = both.sine / both.cosine;
    return RoundToF32(angle < 0.0f ? -tangent : tangent);
}

f32 Asin(f32 value) {
    if (IsNaN(value)) {
        return value;
    }
    if (Abs(value) > 1.0f) {
        return QuietNaN<f32>();
    }
    if (value == 0.0f) {
        return value;
    }
    // asin(x) = atan(x / sqrt(1 - x^2)). x^2 is exact in f64, and so is 1 - x^2 near 1, where it matters.
    const auto x = static_cast<f64>(value);
    return RoundToF32(Atan64(x / Sqrt(1.0 - x * x)));
}

f32 Acos(f32 value) {
    if (IsNaN(value)) {
        return value;
    }
    if (Abs(value) > 1.0f) {
        return QuietNaN<f32>();
    }
    const auto x = static_cast<f64>(value);
    if (x == 0.0) {
        return RoundToF32(kHalfPi64);
    }
    // acos(x) = atan(sqrt(1 - x^2) / x) for x > 0 and pi minus that for x < 0, without the cancellation of
    // pi/2 - asin(x) near 1.
    const f64 root = Sqrt(1.0 - x * x);
    if (x > 0.0) {
        return root == 0.0 ? 0.0f : RoundToF32(Atan64(root / x));
    }
    return RoundToF32(root == 0.0 ? kPi64 : kPi64 - Atan64(root / -x));
}

f32 Atan(f32 value) {
    if (IsNaN(value)) {
        return value;
    }
    if (value == 0.0f) {
        return value;
    }
    return RoundToF32(Atan64(static_cast<f64>(value)));
}

f32 Atan2(f32 y, f32 x) {
    if (IsNaN(y) || IsNaN(x)) {
        return QuietNaN<f32>();
    }
    const bool negativeY = (BitCast<u32>(y) >> 31U) != 0;
    const bool negativeX = (BitCast<u32>(x) >> 31U) != 0;
    f64 angle = 0.0;
    if (y == 0.0f || (IsInfinite(x) && !IsInfinite(y))) {
        // On the x axis, or infinitely far along it.
        angle = negativeX ? kPi64 : 0.0;
    } else if (x == 0.0f) {
        angle = kHalfPi64;
    } else if (IsInfinite(y)) {
        angle = IsInfinite(x) ? (negativeX ? 3.0 * kQuarterPi64 : kQuarterPi64) : kHalfPi64;
    } else {
        // f32 quotients cannot overflow or underflow in f64.
        angle = Atan64(Abs(static_cast<f64>(y)) / Abs(static_cast<f64>(x)));
        if (negativeX) {
            angle = kPi64 - angle;
        }
    }
    const f32 rounded = RoundToF32(angle);
    return negativeY ? -rounded : rounded;
}

f32 Exp(f32 value) {
    if (IsNaN(value)) {
        return value;
    }
    return RoundToF32(Exp64(static_cast<f64>(value)));
}

f32 Exp2(f32 value) {
    if (IsNaN(value)) {
        return value;
    }
    if (value > 128.0f) {
        return Infinity<f32>();
    }
    if (value < -150.0f) {
        return 0.0f;
    }
    // value = k + r exactly, with |r| <= 1/2; 2^r = exp(r ln 2).
    const auto x = static_cast<f64>(value);
    const f64 k = Round(x);
    return RoundToF32(ScaleByPowerOfTwo(ExpSeries((x - k) * kLn2), static_cast<i32>(k)));
}

f32 Log(f32 value) {
    if (IsNaN(value) || value == Infinity<f32>()) {
        return value;
    }
    if (value < 0.0f) {
        return QuietNaN<f32>();
    }
    if (value == 0.0f) {
        return -Infinity<f32>();
    }
    return RoundToF32(Log64(static_cast<f64>(value)));
}

f32 Log2(f32 value) {
    if (IsNaN(value) || value == Infinity<f32>()) {
        return value;
    }
    if (value < 0.0f) {
        return QuietNaN<f32>();
    }
    if (value == 0.0f) {
        return -Infinity<f32>();
    }
    const LogParts parts = SplitLog(static_cast<f64>(value));
    return RoundToF32(parts.exponent + parts.logMantissa * kInvLn2);
}

f32 Pow(f32 base, f32 exponent) {
    // The special cases of IEEE-754 pow, in its order.
    if (exponent == 0.0f || base == 1.0f) {
        return 1.0f;
    }
    if (IsNaN(base) || IsNaN(exponent)) {
        return QuietNaN<f32>();
    }
    const bool oddExponent = IsOddInteger(exponent);
    if (base == 0.0f) {
        if (exponent < 0.0f) {
            return oddExponent ? CopySign(Infinity<f32>(), base) : Infinity<f32>();
        }
        return oddExponent ? base : 0.0f;
    }
    if (IsInfinite(exponent)) {
        const f32 magnitude = Abs(base);
        if (magnitude == 1.0f) {
            return 1.0f;
        }
        return (magnitude < 1.0f) == (exponent < 0.0f) ? Infinity<f32>() : 0.0f;
    }
    if (IsInfinite(base)) {
        const f32 result = exponent < 0.0f ? 0.0f : Infinity<f32>();
        return base < 0.0f && oddExponent ? -result : result;
    }
    if (base < 0.0f && !IsInteger(exponent)) {
        return QuietNaN<f32>();
    }

    const f64 magnitude = Exp64(static_cast<f64>(exponent) * Log64(Abs(static_cast<f64>(base))));
    return RoundToF32(base < 0.0f && oddExponent ? -magnitude : magnitude);
}

f32 Cbrt(f32 value) {
    if (!IsFinite(value) || value == 0.0f) {
        return value;
    }
    const f64 magnitude = Exp64(Log64(Abs(static_cast<f64>(value))) / 3.0);
    return RoundToF32(value < 0.0f ? -magnitude : magnitude);
}

f32 Tanh(f32 value) {
    if (IsNaN(value)) {
        return value;
    }
    const f64 magnitude = Abs(static_cast<f64>(value));
    if (magnitude > 9.5) {
        // 1 - tanh(9.5) is below half an f32 unit at 1.
        return CopySign(1.0f, value);
    }
    // tanh(a) = -m / (2 + m) with m = exp(-2a) - 1; the series avoids cancellation for small a.
    const f64 minusOne = magnitude < 0.55 ? Expm1Series(-2.0 * magnitude) : Exp64(-2.0 * magnitude) - 1.0;
    const f64 result = -minusOne / (2.0 + minusOne);
    return CopySign(RoundToF32(result), value);
}

} // namespace Nexyl::Math
