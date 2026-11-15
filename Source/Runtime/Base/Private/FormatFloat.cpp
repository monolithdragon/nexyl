#include "BigInteger.h"
#include "FormatInternal.h"

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>

// Exact decimal conversion of f32 and f64 with arbitrary-precision integers on the stack.
//
// The shortest form follows Steele and White, "How to Print Floating-Point Numbers Accurately" (1990), in the
// formulation of Burger and Dybvig, "Printing Floating-Point Numbers Quickly and Accurately" (1996): it produces
// the fewest digits that read back to the same value. The fixed-precision forms generate the exact decimal
// expansion and round it half to even. Both use only integer arithmetic, so every platform prints the same text.

namespace Nexyl::Detail {

namespace {

// Up to 1280 bits: enough for 2^1077 times a power of ten that brings it next to the scale.
using BigInteger = Detail::BigInteger<40>;

// The parts of a binary floating-point value: |value| = mantissa * 2^exponent.
struct Decomposed {
    u64 mantissa = 0;
    i32 exponent = 0;
    bool negative = false;
    bool infinite = false;
    bool notANumber = false;
    // The gap to the next smaller value is half the gap to the next larger one (a power of two above the
    // smallest normal exponent).
    bool lowerGapSmaller = false;
};

[[nodiscard]] Decomposed Decompose(u64 bits, usize size) {
    const bool single = size == sizeof(f32);
    const u32 fractionBits = single ? 23U : 52U;
    const u32 exponentBits = single ? 8U : 11U;
    const i32 bias = single ? 150 : 1075; // exponent bias plus fraction bits
    const u64 fractionMask = (u64{1} << fractionBits) - 1U;
    const u64 exponentMask = (u64{1} << exponentBits) - 1U;

    Decomposed value;
    value.negative = ((bits >> (fractionBits + exponentBits)) & 1U) != 0;
    const u64 biased = (bits >> fractionBits) & exponentMask;
    const u64 fraction = bits & fractionMask;
    if (biased == exponentMask) {
        value.infinite = fraction == 0;
        value.notANumber = fraction != 0;
    } else if (biased == 0) {
        value.mantissa = fraction;
        value.exponent = 1 - bias;
    } else {
        value.mantissa = fraction | (u64{1} << fractionBits);
        value.exponent = static_cast<i32>(biased) - bias;
        value.lowerGapSmaller = fraction == 0 && biased > 1;
    }
    return value;
}

// Decimal digits: the value is 0.text * 10^exponent.
struct Digits {
    // Fixed notation needs up to 310 integer digits and kMaxFormatPrecision fraction digits.
    static constexpr usize kCapacity = 832;
    char text[kCapacity] = {};
    usize count = 0;
    i32 exponent = 0;

    void Push(u32 digit) {
        NX_ASSERT(count < kCapacity && digit < 10, "Digit buffer overflow.");
        text[count++] = static_cast<char>('0' + digit);
    }
};

// An estimate of floor(log10(mantissa * 2^exponent)), off by at most one; the callers correct it in either direction.
[[nodiscard]] i32 EstimateDecimalExponent(u64 mantissa, i32 exponent) {
    const auto bitLength = static_cast<i32>(64U - CountLeadingZeros(mantissa));
    // 78913 / 2^18 is just below log10(2). Divide rounding towards negative infinity.
    constexpr i64 kScale = 262'144; // 2^18
    const i64 product = static_cast<i64>(exponent + bitLength - 1) * 78913;
    const i64 quotient = product >= 0 ? product / kScale : -((-product + kScale - 1) / kScale);
    return static_cast<i32>(quotient);
}

void ScaleByPowerOfTen(i32 decimalExponent, BigInteger& scale, BigInteger* values[], usize valueCount) {
    if (decimalExponent >= 0) {
        scale.MultiplyPowerOfTen(static_cast<u32>(decimalExponent));
    } else {
        for (usize index = 0; index < valueCount; ++index) {
            values[index]->MultiplyPowerOfTen(static_cast<u32>(-decimalExponent));
        }
    }
}

// The shortest digits that read back to the same value. `value` is finite and not zero.
void ShortestDigits(const Decomposed& value, Digits& out) {
    BigInteger remainder;
    BigInteger scale;
    BigInteger plus;
    BigInteger minus;
    remainder.Set(value.mantissa);
    scale.Set(1);
    plus.Set(1);
    minus.Set(1);
    // remainder / scale is the value, plus / scale and minus / scale the half-gaps to its neighbours, all doubled
    // so that they are integers.
    if (value.exponent >= 0) {
        const auto shift = static_cast<u32>(value.exponent);
        if (!value.lowerGapSmaller) {
            remainder.ShiftLeft(shift + 1U);
            scale.Set(2);
            plus.ShiftLeft(shift);
            minus.ShiftLeft(shift);
        } else {
            remainder.ShiftLeft(shift + 2U);
            scale.Set(4);
            plus.ShiftLeft(shift + 1U);
            minus.ShiftLeft(shift);
        }
    } else if (!value.lowerGapSmaller) {
        remainder.ShiftLeft(1);
        scale.ShiftLeft(static_cast<u32>(1 - value.exponent));
    } else {
        remainder.ShiftLeft(2);
        scale.ShiftLeft(static_cast<u32>(2 - value.exponent));
        plus.Set(2);
    }
    // With an even mantissa, a reader rounding half to even maps the midpoints to this value too.
    const bool inclusive = (value.mantissa & 1U) == 0;
    const auto reachesHigh = [&scale, inclusive](const BigInteger& low, const BigInteger& gap) {
        BigInteger sum = low;
        sum.Add(gap);
        const i32 order = BigInteger::Compare(sum, scale);
        return inclusive ? order >= 0 : order > 0;
    };

    i32 decimalExponent = EstimateDecimalExponent(value.mantissa, value.exponent);
    BigInteger* scaled[] = {&remainder, &plus, &minus};
    ScaleByPowerOfTen(decimalExponent, scale, scaled, 3);
    // Make decimalExponent the smallest k with value + gap below 10^k.
    while (reachesHigh(remainder, plus)) {
        scale.MultiplySmall(10);
        ++decimalExponent;
    }
    for (;;) {
        BigInteger remainderTimesTen = remainder;
        remainderTimesTen.MultiplySmall(10);
        BigInteger plusTimesTen = plus;
        plusTimesTen.MultiplySmall(10);
        if (reachesHigh(remainderTimesTen, plusTimesTen)) {
            break;
        }
        remainder = remainderTimesTen;
        plus = plusTimesTen;
        minus.MultiplySmall(10);
        --decimalExponent;
    }

    out.count = 0;
    out.exponent = decimalExponent;
    for (;;) {
        remainder.MultiplySmall(10);
        plus.MultiplySmall(10);
        minus.MultiplySmall(10);
        u32 digit = remainder.DivideSmall(scale);
        const i32 lowOrder = BigInteger::Compare(remainder, minus);
        const bool low = inclusive ? lowOrder <= 0 : lowOrder < 0;
        const bool high = reachesHigh(remainder, plus);
        if (!low && !high) {
            out.Push(digit);
            continue;
        }
        if (low && high) {
            // Both neighbours of the last digit read back correctly: take the closer one, the even one on a tie.
            BigInteger twice = remainder;
            twice.ShiftLeft(1);
            const i32 order = BigInteger::Compare(twice, scale);
            if (order > 0 || (order == 0 && (digit & 1U) != 0)) {
                ++digit;
            }
        } else if (high) {
            ++digit;
        }
        out.Push(digit);
        return;
    }
}

// Adds one unit in the last digit after rounding.
void RoundUp(Digits& digits, bool fixed) {
    usize index = digits.count;
    while (index > 0 && digits.text[index - 1] == '9') {
        digits.text[index - 1] = '0';
        --index;
    }
    if (index > 0) {
        ++digits.text[index - 1];
        return;
    }
    // Every digit carried: the value is now 10^exponent, that is 0.1 * 10^(exponent + 1).
    ++digits.exponent;
    if (digits.count == 0) {
        digits.Push(1);
        return;
    }
    digits.text[0] = '1';
    if (fixed) {
        // One more integer digit, the same number of fraction digits.
        digits.Push(0);
    }
}

// The exact digits rounded half to even: `precision + 1` significant digits, or with `fixed` every digit down to
// 10^-precision. `value` is finite and not zero. In fixed notation, no digits means the value rounds to zero.
void ExactDigits(const Decomposed& value, bool fixed, u32 precision, Digits& out) {
    BigInteger remainder;
    BigInteger scale;
    remainder.Set(value.mantissa);
    scale.Set(1);
    if (value.exponent >= 0) {
        remainder.ShiftLeft(static_cast<u32>(value.exponent));
    } else {
        scale.ShiftLeft(static_cast<u32>(-value.exponent));
    }
    i32 decimalExponent = EstimateDecimalExponent(value.mantissa, value.exponent);
    BigInteger* scaled[] = {&remainder};
    ScaleByPowerOfTen(decimalExponent, scale, scaled, 1);
    // Make decimalExponent the smallest k with value below 10^k.
    while (BigInteger::Compare(remainder, scale) >= 0) {
        scale.MultiplySmall(10);
        ++decimalExponent;
    }
    for (;;) {
        BigInteger remainderTimesTen = remainder;
        remainderTimesTen.MultiplySmall(10);
        if (BigInteger::Compare(remainderTimesTen, scale) >= 0) {
            break;
        }
        remainder = remainderTimesTen;
        --decimalExponent;
    }

    out.count = 0;
    out.exponent = decimalExponent;
    const i64 count = fixed ? i64{decimalExponent} + precision : i64{precision} + 1;
    if (count < 0) {
        // Below half a unit of the last requested digit.
        return;
    }
    for (i64 index = 0; index < count; ++index) {
        remainder.MultiplySmall(10);
        out.Push(remainder.DivideSmall(scale));
    }
    BigInteger twice = remainder;
    twice.ShiftLeft(1);
    const i32 order = BigInteger::Compare(twice, scale);
    const bool lastOdd = out.count > 0 && (out.text[out.count - 1] - '0') % 2 != 0;
    if (order > 0 || (order == 0 && lastOdd)) {
        RoundUp(out, fixed);
    }
}

class Body final {
public:
    void Push(char character) {
        NX_ASSERT(m_size < kCapacity, "Formatted number too long.");
        m_text[m_size++] = character;
    }

    void Push(const char* text, usize size) {
        for (usize index = 0; index < size; ++index) {
            Push(text[index]);
        }
    }

    [[nodiscard]] const char* Data() const {
        return m_text;
    }

    [[nodiscard]] usize Size() const {
        return m_size;
    }

private:
    static constexpr usize kCapacity = 1024;
    char m_text[kCapacity] = {};
    usize m_size = 0;
};

// The digit at position `index` (0 is the first), or '0' past either end.
[[nodiscard]] char DigitAt(const Digits& digits, i64 index) {
    return index >= 0 && index < static_cast<i64>(digits.count) ? digits.text[index] : '0';
}

void RenderFixed(const Digits& digits, i64 fractionDigits, Body& body) {
    const i64 integerDigits = digits.count > 0 ? digits.exponent : 0;
    if (integerDigits <= 0) {
        body.Push('0');
    } else {
        for (i64 index = 0; index < integerDigits; ++index) {
            body.Push(DigitAt(digits, index));
        }
    }
    if (fractionDigits > 0) {
        body.Push('.');
        const i64 first = digits.count > 0 ? digits.exponent : 0;
        for (i64 index = 0; index < fractionDigits; ++index) {
            body.Push(DigitAt(digits, first + index));
        }
    }
}

[[nodiscard]] usize ExponentDigitCount(i32 exponent) {
    const u32 magnitude = exponent < 0 ? static_cast<u32>(-exponent) : static_cast<u32>(exponent);
    return magnitude >= 100 ? 3 : 2;
}

void RenderScientific(const Digits& digits, i64 fractionDigits, bool upper, Body& body) {
    body.Push(DigitAt(digits, 0));
    if (fractionDigits > 0) {
        body.Push('.');
        for (i64 index = 1; index <= fractionDigits; ++index) {
            body.Push(DigitAt(digits, index));
        }
    }
    const i32 exponent = digits.count > 0 ? digits.exponent - 1 : 0;
    body.Push(upper ? 'E' : 'e');
    body.Push(exponent < 0 ? '-' : '+');
    const u32 magnitude = exponent < 0 ? static_cast<u32>(-exponent) : static_cast<u32>(exponent);
    if (magnitude >= 100) {
        body.Push(static_cast<char>('0' + (magnitude / 100)));
    }
    body.Push(static_cast<char>('0' + ((magnitude / 10) % 10)));
    body.Push(static_cast<char>('0' + (magnitude % 10)));
}

// Drops trailing zero digits, keeping at least one.
void TrimTrailingZeros(Digits& digits) {
    while (digits.count > 1 && digits.text[digits.count - 1] == '0') {
        --digits.count;
    }
}

// The shortest digits in fixed or scientific notation, whichever is shorter; fixed on a tie.
void RenderShortest(const Digits& digits, bool upper, Body& body) {
    const auto count = static_cast<i64>(digits.count);
    const i64 exponent = digits.exponent;
    const auto scientificLength =
        static_cast<i64>(count + (count > 1 ? 1 : 0) + 2 + static_cast<i64>(ExponentDigitCount(digits.exponent - 1)));
    i64 fixedLength = 0;
    if (exponent <= 0) {
        fixedLength = 2 - exponent + count;
    } else if (exponent >= count) {
        fixedLength = exponent;
    } else {
        fixedLength = count + 1;
    }
    if (fixedLength <= scientificLength) {
        RenderFixed(digits, count > exponent ? count - exponent : 0, body);
    } else {
        RenderScientific(digits, count - 1, upper, body);
    }
}

void RenderGeneral(const Decomposed& value, u32 precision, bool upper, Body& body) {
    const u32 significant = precision == 0 ? 1U : precision;
    if (value.mantissa == 0) {
        body.Push('0');
        return;
    }
    Digits digits;
    ExactDigits(value, false, significant - 1U, digits);
    const i64 exponent = digits.exponent - 1;
    TrimTrailingZeros(digits);
    const auto count = static_cast<i64>(digits.count);
    if (exponent >= -4 && exponent < static_cast<i64>(significant)) {
        RenderFixed(digits, count > digits.exponent ? count - digits.exponent : 0, body);
    } else {
        RenderScientific(digits, count - 1, upper, body);
    }
}

} // namespace

void FormatFloatingPoint(FormatWriter& writer, const FormatSpec& spec, u64 bits, usize size) {
    const Decomposed value = Decompose(bits, size);
    const char type = spec.type;
    const bool upper = type == 'E' || type == 'F' || type == 'G';

    char prefix[1] = {};
    usize prefixSize = 0;
    if (value.negative) {
        prefix[prefixSize++] = '-';
    } else if (spec.sign == FormatSign::Plus) {
        prefix[prefixSize++] = '+';
    } else if (spec.sign == FormatSign::Space) {
        prefix[prefixSize++] = ' ';
    }

    if (value.infinite || value.notANumber) {
        const char* const text = value.infinite ? (upper ? "INF" : "inf") : (upper ? "NAN" : "nan");
        WriteNumber(writer, spec, prefix, prefixSize, text, 3, false);
        return;
    }

    Body body;
    const bool zero = value.mantissa == 0;
    constexpr u32 kDefaultPrecision = 6;
    const u32 precision = spec.hasPrecision ? spec.precision : kDefaultPrecision;
    Digits digits;
    if (type == 'f' || type == 'F') {
        if (!zero) {
            ExactDigits(value, true, precision, digits);
        }
        RenderFixed(digits, precision, body);
    } else if (type == 'e' || type == 'E') {
        if (!zero) {
            ExactDigits(value, false, precision, digits);
        }
        RenderScientific(digits, precision, upper, body);
    } else if (type == 'g' || type == 'G' || spec.hasPrecision) {
        RenderGeneral(value, precision, upper, body);
    } else if (zero) {
        body.Push('0');
    } else {
        ShortestDigits(value, digits);
        RenderShortest(digits, upper, body);
    }
    WriteNumber(writer, spec, prefix, prefixSize, body.Data(), body.Size());
}

} // namespace Nexyl::Detail
