#include <Nexyl/Base/BaseErrors.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Format.h>
#include <Nexyl/Base/InitializerList.h>
#include <Nexyl/Base/Parse.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

namespace {

// Reference fingerprints from Scripts/format/parse_fingerprints.py, which parses the same strings with the
// correctly rounded conversions of the Python standard library.
constexpr u64 kF64ParseFingerprint = 0x81E6'878D'AE40'138CULL;
constexpr u64 kF32ParseFingerprint = 0x8EDF'44FD'EB1E'ECABULL;
constexpr u64 kSeed = 0x4E58'5041'5253'0001ULL;
constexpr i32 kCount = 20'000;
constexpr u64 kOverflow = ~u64{0};

template <usize N>
[[nodiscard]] Span<const char> Text(const char (&literal)[N]) {
    return Span<const char>(literal, N - 1);
}

[[nodiscard]] u64 F64Bits(Span<const char> text) {
    const Result<f64, Error> parsed = ParseF64(text);
    return parsed.IsOk() ? BitCast<u64>(parsed.GetValue()) : kOverflow;
}

[[nodiscard]] u64 F32Bits(Span<const char> text) {
    const Result<f32, Error> parsed = ParseF32(text);
    return parsed.IsOk() ? u64{BitCast<u32>(parsed.GetValue())} : kOverflow;
}

template <typename T>
[[nodiscard]] bool FailsWith(const Result<T, Error>& result, BaseError code) {
    return result.IsError() && result.GetError().Is(code);
}

class SplitMix64 final {
public:
    explicit SplitMix64(u64 seed) : m_state(seed) {}

    u64 Next() {
        m_state += 0x9E37'79B9'7F4A'7C15ULL;
        u64 z = m_state;
        z = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
        return z ^ (z >> 31U);
    }

private:
    u64 m_state = 0;
};

class Fingerprint final {
public:
    void Add(u64 bits) {
        for (u32 byte = 0; byte < 8; ++byte) {
            m_value = (m_value ^ ((bits >> (byte * 8U)) & 0xFFU)) * 0x100'0000'01B3ULL;
        }
    }

    [[nodiscard]] u64 Value() const {
        return m_value;
    }

private:
    u64 m_value = 0xCBF2'9CE4'8422'2325ULL;
};

// The same strings as make_text in Scripts/format/parse_fingerprints.py.
usize MakeText(SplitMix64& generator, char (&text)[64]) {
    const u64 digitCount = 1 + (generator.Next() % 40U);
    char digits[41] = {};
    for (u64 index = 0; index < digitCount; ++index) {
        digits[index] = static_cast<char>('0' + (generator.Next() % 10U));
    }
    const u64 point = generator.Next() % (digitCount + 1);
    usize length = 0;
    for (u64 index = 0; index < digitCount; ++index) {
        if (index == point && point != 0) {
            text[length++] = '.';
        }
        text[length++] = digits[index];
    }
    const auto exponent = static_cast<i64>(generator.Next() % 721U) - 360;
    const FormatResult written = Format(Span<char>(text + length, sizeof(text) - length), "e{}", exponent);
    return length + written.size;
}

} // namespace

NX_TEST(Parse, ReadsIntegers) {
    NX_CHECK(ParseU64(Text("0")).GetValue() == 0);
    NX_CHECK(ParseU64(Text("007")).GetValue() == 7);
    NX_CHECK(ParseU64(Text("18446744073709551615")).GetValue() == ~u64{0});
    NX_CHECK(FailsWith(ParseU64(Text("18446744073709551616")), BaseError::OutOfRange));
    NX_CHECK(ParseI64(Text("-9223372036854775808")).GetValue() == -9223372036854775807LL - 1);
    NX_CHECK(ParseI64(Text("9223372036854775807")).GetValue() == 9223372036854775807LL);
    NX_CHECK(FailsWith(ParseI64(Text("9223372036854775808")), BaseError::OutOfRange));
    NX_CHECK(ParseI64(Text("-0")).GetValue() == 0);
    NX_CHECK(FailsWith(ParseI64(Text("")), BaseError::InvalidArgument));
    NX_CHECK(FailsWith(ParseI64(Text("-")), BaseError::InvalidArgument));
    NX_CHECK(FailsWith(ParseI64(Text("+1")), BaseError::InvalidArgument));
    NX_CHECK(FailsWith(ParseU64(Text("1 ")), BaseError::InvalidArgument));
    NX_CHECK(FailsWith(ParseU64(Text("-1")), BaseError::InvalidArgument));
}

NX_TEST(Parse, RoundsF64Correctly) {
    NX_CHECK(F64Bits(Text("0")) == 0);
    NX_CHECK(F64Bits(Text("-0")) == 0x8000'0000'0000'0000ULL);
    NX_CHECK(F64Bits(Text("0.1")) == 0x3FB9'9999'9999'999AULL);
    NX_CHECK(F64Bits(Text("1")) == 0x3FF0'0000'0000'0000ULL);
    NX_CHECK(F64Bits(Text("0.000e5")) == 0);
    // Halfway between 2^53 and 2^53 + 2: ties to even.
    NX_CHECK(F64Bits(Text("9007199254740993")) == 0x4340'0000'0000'0000ULL);
    NX_CHECK(F64Bits(Text("9007199254740993.000000000000000000001")) == 0x4340'0000'0000'0001ULL);
    NX_CHECK(F64Bits(Text("2.2250738585072011e-308")) == 0x000F'FFFF'FFFF'FFFFULL);
    NX_CHECK(F64Bits(Text("4.9e-324")) == 1);
    NX_CHECK(F64Bits(Text("2.5e-324")) == 1);
    NX_CHECK(F64Bits(Text("2.4e-324")) == 0);
    NX_CHECK(F64Bits(Text("-1e-400")) == 0x8000'0000'0000'0000ULL);
    NX_CHECK(F64Bits(Text("1.7976931348623158e308")) == 0x7FEF'FFFF'FFFF'FFFFULL);
    NX_CHECK(FailsWith(ParseF64(Text("1.7976931348623159e308")), BaseError::OutOfRange));
    NX_CHECK(FailsWith(ParseF64(Text("1e400")), BaseError::OutOfRange));
    NX_CHECK(F64Bits(Text("1e-99999999999")) == 0);
    NX_CHECK(F64Bits(Text("inf")) == 0x7FF0'0000'0000'0000ULL);
    NX_CHECK(F64Bits(Text("-inf")) == 0xFFF0'0000'0000'0000ULL);
    NX_CHECK(F64Bits(Text("nan")) == 0x7FF8'0000'0000'0000ULL);
    NX_CHECK(F64Bits(Text("-nan")) == 0xFFF8'0000'0000'0000ULL);
}

NX_TEST(Parse, RejectsMalformedNumbers) {
    for (const Span<const char> text :
         {Text(""), Text("-"), Text("1."), Text(".5"), Text("1e"), Text("1e+"), Text("+1"), Text("1x"), Text(" 1"),
          Text("infinity"), Text("NaN"), Text("1.2.3"), Text("--1"), Text("e5")}) {
        NX_CHECK(FailsWith(ParseF64(text), BaseError::InvalidArgument));
    }
}

NX_TEST(Parse, KeepsTheEffectOfDigitsBeyondEightHundred) {
    // 2^53 + 1 followed by 800 zeros and a 1: just above halfway, so it rounds up, which only the last digit shows.
    char text[900] = {};
    const char prefix[] = "9007199254740993.";
    usize length = 0;
    for (const char character : Text(prefix)) {
        text[length++] = character;
    }
    for (u32 index = 0; index < 800; ++index) {
        text[length++] = '0';
    }
    text[length++] = '1';
    NX_CHECK(F64Bits(Span<const char>(text, length)) == 0x4340'0000'0000'0001ULL);
    NX_CHECK(F64Bits(Span<const char>(text, length - 1)) == 0x4340'0000'0000'0000ULL);
}

NX_TEST(Parse, RoundsF32Correctly) {
    NX_CHECK(F32Bits(Text("0.1")) == 0x3DCC'CCCDU);
    NX_CHECK(F32Bits(Text("3.4028235e38")) == 0x7F7F'FFFFU);
    NX_CHECK(FailsWith(ParseF32(Text("3.4028236e38")), BaseError::OutOfRange));
    NX_CHECK(F32Bits(Text("1.4e-45")) == 1);
    NX_CHECK(F32Bits(Text("1e-46")) == 0);
    NX_CHECK(F32Bits(Text("16777217")) == 0x4B80'0000U); // halfway: ties to even
    NX_CHECK(FailsWith(ParseF32(Text("1e300")), BaseError::OutOfRange));
}

NX_TEST(Parse, ReadsBackWhatFormatWrites) {
    SplitMix64 generator(kSeed);
    i32 mismatches = 0;
    for (i32 index = 0; index < kCount; ++index) {
        const u64 bits = generator.Next();
        const auto bits32 = static_cast<u32>(bits >> 32U);
        char text[64] = {};
        if (((bits >> 52U) & 0x7FFU) != 0x7FFU) {
            const f64 value = BitCast<f64>(bits);
            const FormatResult shortest = Format(text, "{}", value);
            mismatches += F64Bits(Span<const char>(text, shortest.size)) == bits ? 0 : 1;
            const FormatResult scientific = Format(text, "{:.16e}", value);
            mismatches += F64Bits(Span<const char>(text, scientific.size)) == bits ? 0 : 1;
        }
        if (((bits32 >> 23U) & 0xFFU) != 0xFFU) {
            const f32 value = BitCast<f32>(bits32);
            const FormatResult shortest = Format(text, "{}", value);
            mismatches += F32Bits(Span<const char>(text, shortest.size)) == bits32 ? 0 : 1;
        }
    }
    NX_CHECK(mismatches == 0);
}

NX_TEST(Parse, MatchesTheReferenceConversions) {
    SplitMix64 generator(kSeed);
    Fingerprint f64Fingerprint;
    Fingerprint f32Fingerprint;
    for (i32 index = 0; index < kCount; ++index) {
        char text[64] = {};
        const usize length = MakeText(generator, text);
        f64Fingerprint.Add(F64Bits(Span<const char>(text, length)));
        f32Fingerprint.Add(F32Bits(Span<const char>(text, length)));
    }
    NX_CHECK(f64Fingerprint.Value() == kF64ParseFingerprint);
    NX_CHECK(f32Fingerprint.Value() == kF32ParseFingerprint);
}
