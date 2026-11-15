#include <Nexyl/Base/Format.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

namespace {

// Holds formatted text for comparison with a literal.
struct Formatted {
    char text[256] = {};
    FormatResult result;

    [[nodiscard]] bool Equals(const char* expected) const {
        usize length = 0;
        while (expected[length] != '\0') {
            ++length;
        }
        if (length != result.size) {
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

enum class Color : u8 {
    Red = 1,
    Blue = 7,
};

enum class Offset : i8 {
    Back = -3,
};

struct Point {
    i32 x = 0;
    i32 y = 0;
};

void FormatValue(FormatWriter& writer, const FormatSpec& spec, const Point& point) {
    static_cast<void>(spec);
    FormatTo(writer, "({}, {})", point.x, point.y);
}

template <typename... T>
constexpr bool IsValid(const char* text, usize size) {
    constexpr Detail::FormatCategory kCategories[] = {Detail::FormatCategoryOf<T>()..., Detail::FormatCategory::Custom};
    return Detail::ValidateFormat(text, size, kCategories, sizeof...(T)) == nullptr;
}

template <typename... T, usize N>
constexpr bool IsValid(const char (&text)[N]) {
    return IsValid<T...>(text, N - 1);
}

} // namespace

// The checks below run at compile time; each one would be a compile error as a real format string.
static_assert(IsValid<>("plain text"));
static_assert(IsValid<i32, i32>("{} and {}"));
static_assert(IsValid<>("{{}}"));
static_assert(!IsValid<i32>("{} {}"));
static_assert(!IsValid<i32, i32>("{}"));
static_assert(!IsValid<i32>("{0}"));
static_assert(!IsValid<i32>("{"));
static_assert(!IsValid<>("}"));
static_assert(!IsValid<i32>("{:q}"));
static_assert(!IsValid<i32>("{:.2}"));
static_assert(!IsValid<const char*>("{:d}"));
static_assert(!IsValid<const char*>("{:+}"));
static_assert(!IsValid<bool>("{:x}"));
static_assert(!IsValid<i32>("{:2000}"));
static_assert(!IsValid<i32>("{:.}"));
static_assert(!IsValid<void*>("{:d}"));
static_assert(IsValid<Point>("{:>8q}"));
static_assert(!IsValid<Point>("{:anything}"));

NX_TEST(Format, WritesLiteralTextAndEscapedBraces) {
    NX_CHECK(FormatToText("hello").Equals("hello"));
    NX_CHECK(FormatToText("{{}} {{x}}").Equals("{} {x}"));
    NX_CHECK(FormatToText("").Equals(""));
}

NX_TEST(Format, FormatsIntegers) {
    NX_CHECK(FormatToText("{}", 0).Equals("0"));
    NX_CHECK(FormatToText("{}", -42).Equals("-42"));
    NX_CHECK(FormatToText("{}", u64{18446744073709551615ULL}).Equals("18446744073709551615"));
    NX_CHECK(FormatToText("{}", i64{-9223372036854775807LL - 1}).Equals("-9223372036854775808"));
    NX_CHECK(FormatToText("{}", i8{-128}).Equals("-128"));
    NX_CHECK(FormatToText("{}", u8{255}).Equals("255"));
    NX_CHECK(FormatToText("{:x} {:X} {:b} {:o}", 255, 255, 5, 8).Equals("ff FF 101 10"));
    NX_CHECK(FormatToText("{:#x} {:#X} {:#b} {:#o}", 255, 255, 5, 8).Equals("0xff 0XFF 0b101 0o10"));
    NX_CHECK(FormatToText("{:+} {:+} {: }", 5, -5, 5).Equals("+5 -5  5"));
    NX_CHECK(FormatToText("{:#x}", -255).Equals("-0xff"));
}

NX_TEST(Format, PadsAndAligns) {
    NX_CHECK(FormatToText("[{:5}]", 42).Equals("[   42]"));
    NX_CHECK(FormatToText("[{:<5}]", 42).Equals("[42   ]"));
    NX_CHECK(FormatToText("[{:^5}]", 42).Equals("[ 42  ]"));
    NX_CHECK(FormatToText("[{:*>6}]", 42).Equals("[****42]"));
    NX_CHECK(FormatToText("[{:05}]", -42).Equals("[-0042]"));
    NX_CHECK(FormatToText("[{:#010x}]", 255).Equals("[0x000000ff]"));
    NX_CHECK(FormatToText("[{:<05}]", 7).Equals("[7    ]"));
    NX_CHECK(FormatToText("[{:1}]", 12345).Equals("[12345]"));
    NX_CHECK(FormatToText("[{:6}]", "ab").Equals("[ab    ]"));
    NX_CHECK(FormatToText("[{:>6}]", "ab").Equals("[    ab]"));
}

NX_TEST(Format, FormatsTextBoolsAndCharacters) {
    const char* text = "pointer";
    const char* none = nullptr;
    const char array[8] = {'a', 'b', '\0', 'z'};
    NX_CHECK(FormatToText("{} {} {}", "literal", text, none).Equals("literal pointer (null)"));
    NX_CHECK(FormatToText("{}", array).Equals("ab"));
    NX_CHECK(FormatToText("{:.3}", "abcdef").Equals("abc"));
    NX_CHECK(FormatToText("{:.10}", "abc").Equals("abc"));
    NX_CHECK(FormatToText("{} {:s}", true, false).Equals("true false"));
    NX_CHECK(FormatToText("{} {:c} {:d} {:#x}", 'A', 'B', 'C', 'D').Equals("A B 67 0x44"));
}

NX_TEST(Format, FormatsEnumerationsAsTheirValue) {
    NX_CHECK(FormatToText("{} {}", Color::Blue, Offset::Back).Equals("7 -3"));
    NX_CHECK(FormatToText("{:#x}", Color::Red).Equals("0x1"));
}

NX_TEST(Format, FormatsPointers) {
    const void* null = nullptr;
    NX_CHECK(FormatToText("{}", null).Equals("0x0"));
    NX_CHECK(FormatToText("{}", nullptr).Equals("0x0"));
    const auto* pointer = BitCast<const void*>(usize{0x1234ABCD});
    NX_CHECK(FormatToText("{} {:p}", pointer, pointer).Equals("0x1234abcd 0x1234abcd"));
}

NX_TEST(Format, CallsCustomFormatters) {
    NX_CHECK(FormatToText("at {}", Point{3, -4}).Equals("at (3, -4)"));
}

NX_TEST(Format, TruncatesAtTheEndOfTheBuffer) {
    char buffer[5] = {'#', '#', '#', '#', '#'};
    const FormatResult fits = Format(Span<char>(buffer, 4), "{}", 1234);
    NX_CHECK(fits.size == 4);
    NX_CHECK(!fits.truncated);

    const FormatResult cut = Format(Span<char>(buffer, 4), "value {}", 1234);
    NX_CHECK(cut.size == 4);
    NX_CHECK(cut.truncated);
    NX_CHECK(buffer[0] == 'v' && buffer[3] == 'u');
    NX_CHECK(buffer[4] == '#');

    const FormatResult padded = Format(Span<char>(buffer, 4), "{:10}", 1);
    NX_CHECK(padded.size == 4);
    NX_CHECK(padded.truncated);

    const FormatResult empty = Format(Span<char>(), "x");
    NX_CHECK(empty.size == 0);
    NX_CHECK(empty.truncated);
}

NX_TEST(FormatWriter, AppendsAcrossCalls) {
    char buffer[16] = {};
    FormatWriter writer(buffer);
    FormatTo(writer, "{}-", 1);
    FormatTo(writer, "{}", 2);
    NX_CHECK(writer.Size() == 3);
    NX_CHECK(buffer[0] == '1' && buffer[1] == '-' && buffer[2] == '2');
    NX_CHECK(!writer.IsTruncated());
}
