#include <Nexyl/Base/Parse.h>

#include "BigInteger.h"

#include <Nexyl/Base/BaseErrors.h>
#include <Nexyl/Base/Utility.h>

// Decimal to binary floating point: an estimate from the leading digits, then exact comparisons of the decimal
// value with the midpoints between neighbouring floating-point values, in big integers, until the value lies
// between the midpoints around the result (ties to the even neighbour). Only multiplication, shifts and
// comparison are needed.

namespace Nexyl {

namespace {

// Enough for 801 digits times 10^1143, or times 2^1075: the extremes the comparisons meet.
using Big = Detail::BigInteger<128>;

constexpr usize kMaxDigits = 800;
constexpr i32 kExponentLimit = 100'000;

[[nodiscard]] bool IsDigit(char character) {
    return character >= '0' && character <= '9';
}

[[nodiscard]] bool Equals(Span<const char> text, usize offset, const char* word) {
    usize index = 0;
    while (word[index] != '\0') {
        if (offset + index >= text.Size() || text[offset + index] != word[index]) {
            return false;
        }
        ++index;
    }
    return offset + index == text.Size();
}

// The decimal number digits * 10^exponent, with up to kMaxDigits significant digits.
struct Decimal {
    char digits[kMaxDigits + 1] = {};
    usize count = 0;
    i32 exponent = 0;
    bool negative = false;
    bool infinite = false;
    bool notANumber = false;
};

Result<void, Error> ParseDecimal(Span<const char> text, Decimal& decimal) {
    usize position = 0;
    if (position < text.Size() && text[position] == '-') {
        decimal.negative = true;
        ++position;
    }
    if (Equals(text, position, "inf")) {
        decimal.infinite = true;
        return {};
    }
    if (Equals(text, position, "nan")) {
        decimal.notANumber = true;
        return {};
    }

    bool anyDigit = false;
    bool truncated = false;
    i32 dropped = 0; // integer digits beyond kMaxDigits, which scale the value
    const auto takeDigit = [&decimal, &truncated](char digit) {
        if (decimal.count == 0 && digit == '0') {
            return false; // a leading zero carries no information
        }
        if (decimal.count < kMaxDigits) {
            decimal.digits[decimal.count++] = digit;
            return false;
        }
        truncated = truncated || digit != '0';
        return true;
    };

    while (position < text.Size() && IsDigit(text[position])) {
        anyDigit = true;
        if (takeDigit(text[position])) {
            ++dropped;
        }
        ++position;
    }
    if (!anyDigit) {
        return Error(BaseError::InvalidArgument);
    }
    i32 fractionDigits = 0;
    if (position < text.Size() && text[position] == '.') {
        ++position;
        bool anyFraction = false;
        while (position < text.Size() && IsDigit(text[position])) {
            anyFraction = true;
            const bool wasCounted = decimal.count > 0;
            if (!takeDigit(text[position]) || !wasCounted) {
                // A kept digit, or a leading zero after the point: either shifts the value.
                if (fractionDigits < kExponentLimit) {
                    ++fractionDigits;
                }
            }
            ++position;
        }
        if (!anyFraction) {
            return Error(BaseError::InvalidArgument);
        }
    }
    i32 exponent = 0;
    if (position < text.Size() && (text[position] == 'e' || text[position] == 'E')) {
        ++position;
        bool negativeExponent = false;
        if (position < text.Size() && (text[position] == '+' || text[position] == '-')) {
            negativeExponent = text[position] == '-';
            ++position;
        }
        bool anyExponent = false;
        while (position < text.Size() && IsDigit(text[position])) {
            anyExponent = true;
            if (exponent < kExponentLimit) {
                exponent = (exponent * 10) + (text[position] - '0');
            }
            ++position;
        }
        if (!anyExponent) {
            return Error(BaseError::InvalidArgument);
        }
        exponent = negativeExponent ? -exponent : exponent;
    }
    if (position != text.Size()) {
        return Error(BaseError::InvalidArgument);
    }

    decimal.exponent = exponent + dropped - fractionDigits;
    if (truncated) {
        // Any digit 1-9 past the kept ones: 800 digits decide the rounding, so a final 1 stands for them all.
        decimal.digits[decimal.count++] = '1';
        --decimal.exponent;
    }
    return {};
}

// The constants of one binary format.
struct BinaryFormat {
    u32 fractionBits = 0;
    i32 bias = 0; // exponent bias plus fraction bits
    u64 exponentMask = 0;
    i32 maxDecimalExponent = 0; // beyond 10^this, every value overflows
    i32 minDecimalExponent = 0; // below 10^this, every value rounds to zero
};

constexpr BinaryFormat kF64Format{52, 1075, 0x7FF, 310, -345};
constexpr BinaryFormat kF32Format{23, 150, 0xFF, 40, -47};

struct Scaled {
    u64 mantissa = 0;
    i32 exponent = 0;
};

// The value of a non-negative bit pattern as mantissa * 2^exponent. The pattern of infinity decodes to the
// power of two above the largest finite value, which is where rounding to infinity starts.
[[nodiscard]] Scaled Decode(u64 bits, const BinaryFormat& format) {
    const u64 fraction = bits & ((u64{1} << format.fractionBits) - 1U);
    const u64 biased = bits >> format.fractionBits;
    if (biased == 0) {
        return Scaled{fraction, 1 - format.bias};
    }
    return Scaled{fraction | (u64{1} << format.fractionBits), static_cast<i32>(biased) - format.bias};
}

// The sign of digits * 10^exponent minus the midpoint between the values of `bits` and `bits + 1`.
[[nodiscard]] i32 CompareWithMidpoint(const Big& digits, i32 exponent, u64 bits, const BinaryFormat& format) {
    const Scaled low = Decode(bits, format);
    const Scaled high = Decode(bits + 1, format);
    // Neighbouring values have the same exponent or, across a power of two, exponents one apart.
    const i32 common = low.exponent < high.exponent ? low.exponent : high.exponent;
    const u64 lowPart = low.exponent > common ? low.mantissa << 1U : low.mantissa;
    const u64 highPart = high.exponent > common ? high.mantissa << 1U : high.mantissa;
    const u64 sum = lowPart + highPart;
    // midpoint = sum * 2^(common - 1)
    Big left = digits;
    Big right;
    right.Set(sum);
    if (exponent >= 0) {
        left.MultiplyPowerOfTen(static_cast<u32>(exponent));
    } else {
        right.MultiplyPowerOfTen(static_cast<u32>(-exponent));
    }
    const i32 power = common - 1;
    if (power >= 0) {
        right.ShiftLeft(static_cast<u32>(power));
    } else {
        left.ShiftLeft(static_cast<u32>(-power));
    }
    return Big::Compare(left, right);
}

// An estimate within a few units in the last place, from the leading digits.
[[nodiscard]] f64 Estimate(const Decimal& decimal) {
    constexpr f64 kPowers[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                               1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const usize leading = decimal.count < 19 ? decimal.count : 19;
    u64 value = 0;
    for (usize index = 0; index < leading; ++index) {
        value = (value * 10U) + static_cast<u64>(decimal.digits[index] - '0');
    }
    // The value is about value * 10^power; scale in steps that keep the intermediate result in range.
    i32 power = decimal.exponent + static_cast<i32>(decimal.count - leading);
    f64 estimate = static_cast<f64>(value);
    while (power > 22) {
        estimate *= 1e22;
        power -= 22;
    }
    while (power < -22) {
        estimate /= 1e22;
        power += 22;
    }
    return power >= 0 ? estimate * kPowers[power] : estimate / kPowers[-power];
}

// The non-negative bit pattern nearest to the decimal value, or an error if it rounds to infinity.
Result<u64, Error> Round(const Decimal& decimal, u64 estimateBits, const BinaryFormat& format) {
    if (decimal.count == 0) {
        return u64{0};
    }
    const i32 magnitude = decimal.exponent + static_cast<i32>(decimal.count);
    if (magnitude > format.maxDecimalExponent) {
        return Error(BaseError::OutOfRange);
    }
    if (magnitude < format.minDecimalExponent) {
        return u64{0};
    }
    Big digits;
    for (usize index = 0; index < decimal.count; ++index) {
        digits.MultiplySmall(10);
        digits.AddSmall(static_cast<u32>(decimal.digits[index] - '0'));
    }

    const u64 infinity = format.exponentMask << format.fractionBits;
    u64 bits = estimateBits < infinity ? estimateBits : infinity - 1;
    for (;;) {
        const i32 above = CompareWithMidpoint(digits, decimal.exponent, bits, format);
        if (above > 0 || (above == 0 && (bits & 1U) != 0)) {
            ++bits;
            if (bits == infinity) {
                return Error(BaseError::OutOfRange);
            }
            continue;
        }
        if (bits > 0) {
            const i32 below = CompareWithMidpoint(digits, decimal.exponent, bits - 1, format);
            if (below < 0 || (below == 0 && (bits & 1U) != 0)) {
                --bits;
                continue;
            }
        }
        return bits;
    }
}

} // namespace

Result<u64, Error> ParseU64(Span<const char> text) {
    if (text.IsEmpty()) {
        return Error(BaseError::InvalidArgument);
    }
    u64 value = 0;
    for (const char character : text) {
        if (!IsDigit(character)) {
            return Error(BaseError::InvalidArgument);
        }
        const auto digit = static_cast<u64>(character - '0');
        if (value > (~u64{0} - digit) / 10U) {
            return Error(BaseError::OutOfRange);
        }
        value = (value * 10U) + digit;
    }
    return value;
}

Result<i64, Error> ParseI64(Span<const char> text) {
    const bool negative = !text.IsEmpty() && text[0] == '-';
    NX_TRY_ASSIGN(const u64 magnitude, ParseU64(negative ? text.Subspan(1) : text));
    constexpr u64 kLimit = u64{1} << 63U;
    if (magnitude > (negative ? kLimit : kLimit - 1U)) {
        return Error(BaseError::OutOfRange);
    }
    // Negate in unsigned arithmetic so that the most negative value does not overflow.
    return negative ? static_cast<i64>(~magnitude + 1U) : static_cast<i64>(magnitude);
}

Result<f64, Error> ParseF64(Span<const char> text) {
    Decimal decimal;
    NX_TRY(ParseDecimal(text, decimal));
    u64 bits = 0;
    if (decimal.notANumber) {
        bits = 0x7FF8'0000'0000'0000ULL;
    } else if (decimal.infinite) {
        bits = 0x7FF0'0000'0000'0000ULL;
    } else {
        const f64 estimate = Estimate(decimal);
        NX_TRY_ASSIGN(const u64 rounded, Round(decimal, BitCast<u64>(estimate), kF64Format));
        bits = rounded;
    }
    if (decimal.negative) {
        bits |= u64{1} << 63U;
    }
    return BitCast<f64>(bits);
}

Result<f32, Error> ParseF32(Span<const char> text) {
    Decimal decimal;
    NX_TRY(ParseDecimal(text, decimal));
    u32 bits = 0;
    if (decimal.notANumber) {
        bits = 0x7FC0'0000U;
    } else if (decimal.infinite) {
        bits = 0x7F80'0000U;
    } else {
        // The f64 estimate, converted, is within a few f32 steps; Round makes it exact. It is clamped first,
        // because converting a value outside the f32 range is undefined.
        constexpr f64 kLargestF32 = 3.4028234663852886e38;
        const f64 wide = Estimate(decimal);
        const auto estimate = static_cast<f32>(wide < kLargestF32 ? wide : kLargestF32);
        NX_TRY_ASSIGN(const u64 rounded, Round(decimal, BitCast<u32>(estimate), kF32Format));
        bits = static_cast<u32>(rounded);
    }
    if (decimal.negative) {
        bits |= u32{1} << 31U;
    }
    return BitCast<f32>(bits);
}

} // namespace Nexyl
