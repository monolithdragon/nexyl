#include <Nexyl/Base/Format.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

namespace {

// Reference fingerprints from Scripts/format/format_fingerprints.py, which formats the same values with the
// correctly rounded conversions of the Python standard library.
constexpr u64 kF64ShortestFingerprint = 0x7C46'B09A'3828'0A35ULL;
constexpr u64 kF64ScientificFingerprint = 0x4326'E6CC'80EC'99FCULL;
constexpr u64 kF64GeneralFingerprint = 0x0029'B8A2'B193'C406ULL;
constexpr u64 kF64FixedFingerprint = 0x53BC'715C'B039'585EULL;
constexpr u64 kF32ShortestFingerprint = 0x242C'D7B1'D4B9'0EACULL;
constexpr u64 kF32ScientificFingerprint = 0x43F4'DBF6'9405'7C96ULL;

constexpr u64 kSeed = 0x4E58'464D'5430'0001ULL;
constexpr i32 kCount = 10'000;

struct Formatted {
    char text[1100] = {};
    FormatResult result;

    [[nodiscard]] bool Equals(const char* expected) const {
        usize length = 0;
        while (expected[length] != '\0') {
            ++length;
        }
        if (length != result.size || result.truncated) {
            return false;
        }
        for (usize index = 0; index < length; ++index) {
            if (text[index] != expected[index]) {
                return false;
            }
        }
        return true;
    }
};

template <Formattable... T>
Formatted FormatToText(FormatString<TypeIdentity<T>...> format, const T&... args) {
    Formatted formatted;
    formatted.result = Format<T...>(formatted.text, format, args...);
    return formatted;
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
    void Add(const char* text, usize size) {
        for (usize index = 0; index < size; ++index) {
            Mix(static_cast<u8>(text[index]));
        }
        Mix(static_cast<u8>('\n'));
    }

    [[nodiscard]] u64 Value() const {
        return m_value;
    }

private:
    void Mix(u8 byte) {
        m_value = (m_value ^ byte) * 0x100'0000'01B3ULL;
    }

    u64 m_value = 0xCBF2'9CE4'8422'2325ULL;
};

template <Formattable T>
void AddFormatted(Fingerprint& fingerprint, FormatString<TypeIdentity<T>> format, const T& value) {
    char buffer[1100] = {};
    const FormatResult result = Format<T>(buffer, format, value);
    fingerprint.Add(buffer, result.size);
}

} // namespace

NX_TEST(FormatFloat, PrintsTheShortestRoundTrip) {
    NX_CHECK(FormatToText("{}", 0.1).Equals("0.1"));
    NX_CHECK(FormatToText("{}", 0.3).Equals("0.3"));
    NX_CHECK(FormatToText("{}", 1.0).Equals("1"));
    NX_CHECK(FormatToText("{}", 100.0).Equals("100"));
    NX_CHECK(FormatToText("{}", 2.5).Equals("2.5"));
    NX_CHECK(FormatToText("{}", -0.125).Equals("-0.125"));
    NX_CHECK(FormatToText("{}", 0.001).Equals("0.001"));
    NX_CHECK(FormatToText("{}", 1e-5).Equals("1e-05"));
    NX_CHECK(FormatToText("{}", 1e16).Equals("1e+16"));
    NX_CHECK(FormatToText("{}", 1e21).Equals("1e+21"));
    NX_CHECK(FormatToText("{}", 123456789012345680.0).Equals("123456789012345680"));
    NX_CHECK(FormatToText("{}", 5e-324).Equals("5e-324"));
    NX_CHECK(FormatToText("{}", 1.7976931348623157e308).Equals("1.7976931348623157e+308"));
    NX_CHECK(FormatToText("{}", 0.1f).Equals("0.1"));
    NX_CHECK(FormatToText("{}", 3.14159265f).Equals("3.1415927"));
    NX_CHECK(FormatToText("{}", 16777216.0f).Equals("16777216"));
    NX_CHECK(FormatToText("{}", 3.40282347e38f).Equals("3.4028235e+38"));
    NX_CHECK(FormatToText("{}", 1.40129846e-45f).Equals("1e-45"));
}

NX_TEST(FormatFloat, PrintsZerosInfinitiesAndNaNs) {
    const f64 infinity = BitCast<f64>(0x7FF0'0000'0000'0000ULL);
    const f64 notANumber = BitCast<f64>(0x7FF8'0000'0000'0000ULL);
    NX_CHECK(FormatToText("{} {}", 0.0, -0.0).Equals("0 -0"));
    NX_CHECK(FormatToText("{:.2f} {:.1e} {:g}", -0.0, 0.0, 0.0).Equals("-0.00 0.0e+00 0"));
    NX_CHECK(FormatToText("{} {} {}", infinity, -infinity, notANumber).Equals("inf -inf nan"));
    NX_CHECK(FormatToText("{:E} {:F}", infinity, notANumber).Equals("INF NAN"));
    NX_CHECK(FormatToText("[{:06}]", infinity).Equals("[   inf]"));
}

NX_TEST(FormatFloat, RoundsFixedPrecisionHalfToEven) {
    NX_CHECK(FormatToText("{:.0f} {:.0f}", 2.5, 3.5).Equals("2 4"));
    NX_CHECK(FormatToText("{:.2f} {:.2f}", 0.125, 0.375).Equals("0.12 0.38"));
    // 1.005 is stored as 1.00499999999999989...: the exact value decides, not its shortest spelling.
    NX_CHECK(FormatToText("{:.2f}", 1.005).Equals("1.00"));
    NX_CHECK(FormatToText("{:.2f}", 9.9951).Equals("10.00"));
    NX_CHECK(FormatToText("{:.3f} {:.3f}", 0.0004, 0.0005).Equals("0.000 0.001"));
    NX_CHECK(FormatToText("{:f}", 3.0).Equals("3.000000"));
    NX_CHECK(FormatToText("{:.0f}", 1e23).Equals("99999999999999991611392"));
    NX_CHECK(FormatToText("{:.3f}", 0.5e-3).Equals("0.001"));
    NX_CHECK(FormatToText("{:.1f}", 0.04).Equals("0.0"));
}

NX_TEST(FormatFloat, PrintsScientificAndGeneral) {
    NX_CHECK(FormatToText("{:.17e}", 0.1).Equals("1.00000000000000006e-01"));
    NX_CHECK(FormatToText("{:e}", 1e300).Equals("1.000000e+300"));
    NX_CHECK(FormatToText("{:.1e}", 9.96).Equals("1.0e+01"));
    NX_CHECK(FormatToText("{:.0e}", 25.0).Equals("2e+01"));
    NX_CHECK(FormatToText("{:E}", 1234.5).Equals("1.234500E+03"));
    NX_CHECK(FormatToText("{:.6g}", 0.0001234).Equals("0.0001234"));
    NX_CHECK(FormatToText("{:g} {:g} {:g}", 1e-5, 123456789.0, 100000.0).Equals("1e-05 1.23457e+08 100000"));
    NX_CHECK(FormatToText("{:.3g}", 0.0).Equals("0"));
    NX_CHECK(FormatToText("{:.0g}", 0.25).Equals("0.2"));
    NX_CHECK(FormatToText("{:G}", 1e-10).Equals("1E-10"));
    // A precision without a type means general notation.
    NX_CHECK(FormatToText("{:.3}", 3.14159).Equals("3.14"));
}

NX_TEST(FormatFloat, PadsAndSigns) {
    NX_CHECK(FormatToText("[{:8.2f}]", 3.14159).Equals("[    3.14]"));
    NX_CHECK(FormatToText("[{:<8.2f}]", 3.14159).Equals("[3.14    ]"));
    NX_CHECK(FormatToText("[{:08.2f}]", -3.14159).Equals("[-0003.14]"));
    NX_CHECK(FormatToText("{:+} {: }", 1.5, 1.5).Equals("+1.5  1.5"));
}

NX_TEST(FormatFloat, HandlesTheLongestOutputs) {
    // 310 integer digits and the largest precision fit the formatter's own buffers.
    const Formatted largest = FormatToText("{:.512f}", -1.7976931348623157e308);
    NX_CHECK(largest.result.size == 1 + 309 + 1 + 512);
    NX_CHECK(!largest.result.truncated);
    const Formatted smallest = FormatToText("{:.512e}", 5e-324);
    NX_CHECK(smallest.result.size == 1 + 1 + 512 + 5);
    NX_CHECK(smallest.text[0] == '4');
}

NX_TEST(FormatFloat, MatchesTheReferenceConversions) {
    SplitMix64 generator(kSeed);
    Fingerprint f64Shortest;
    Fingerprint f64Scientific;
    Fingerprint f64General;
    Fingerprint f64Fixed;
    i32 produced = 0;
    while (produced < kCount) {
        const u64 bits = generator.Next();
        const u64 biased = (bits >> 52U) & 0x7FFU;
        if (biased == 0x7FFU) {
            continue;
        }
        const f64 value = BitCast<f64>(bits);
        AddFormatted(f64Shortest, "{}", value);
        AddFormatted(f64Scientific, "{:.17e}", value);
        AddFormatted(f64General, "{:.6g}", value);
        if ((biased > 1023U ? biased - 1023U : 1023U - biased) <= 70U) {
            AddFormatted(f64Fixed, "{:.3f}", value);
        }
        ++produced;
    }
    NX_CHECK(f64Shortest.Value() == kF64ShortestFingerprint);
    NX_CHECK(f64Scientific.Value() == kF64ScientificFingerprint);
    NX_CHECK(f64General.Value() == kF64GeneralFingerprint);
    NX_CHECK(f64Fixed.Value() == kF64FixedFingerprint);

    Fingerprint f32Shortest;
    Fingerprint f32Scientific;
    produced = 0;
    while (produced < kCount) {
        const auto bits = static_cast<u32>(generator.Next() >> 32U);
        if (((bits >> 23U) & 0xFFU) == 0xFFU) {
            continue;
        }
        const f32 value = BitCast<f32>(bits);
        AddFormatted(f32Shortest, "{}", value);
        AddFormatted(f32Scientific, "{:.8e}", value);
        ++produced;
    }
    NX_CHECK(f32Shortest.Value() == kF32ShortestFingerprint);
    NX_CHECK(f32Scientific.Value() == kF32ScientificFingerprint);
}
