#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Hash.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Containers;

namespace {

enum class Color : u8 { Red, Green };

u64 HashText(StringView text) {
    return HashBytes(text.Data(), text.Size());
}

} // namespace

NX_TEST(Hash, MatchesTheReferenceValuesOnEveryPlatform) {
    // Computed by an independent reference implementation of the algorithm. The same inputs must give the same
    // bits on every compiler and platform, because hash order feeds deterministic simulation (section 7.7).
    char text[100] = {};
    for (usize index = 0; index < sizeof(text); ++index) {
        text[index] = static_cast<char>(32 + index);
    }
    NX_CHECK(HashText("") == 0x545F23DDCFE838C4);
    NX_CHECK(HashText("a") == 0x6A40478B6D07690B);
    NX_CHECK(HashText("abc") == 0x545C34ECAF112968);
    NX_CHECK(HashText("hello world") == 0x31EB491196FFBF09);
    NX_CHECK(HashText(StringView(text, 16)) == 0x8F0BF0C9B603C8A4);
    NX_CHECK(HashText(StringView(text, 17)) == 0xFF5D913F9C6B89F3);
    NX_CHECK(HashText(StringView(text, 48)) == 0x704205046A98F3FA);
    NX_CHECK(HashText(StringView(text, 100)) == 0x08A8CE5ABD63BC14);

    NX_CHECK(HashInteger(0) == 0xACFE59E18F146E55);
    NX_CHECK(HashInteger(1) == 0xE100CE7500350887);
    NX_CHECK(HashInteger(~u64{0}) == 0x46A3348CDCC3EE46);
}

NX_TEST(Hash, SpreadsConsecutiveIntegersEvenly) {
    // 64 buckets by the low bits, as a hash table uses them; 64 000 keys give 1000 per bucket on average.
    u32 buckets[64] = {};
    for (u32 value = 0; value < 64'000; ++value) {
        ++buckets[HashValue(value) & 63U];
    }
    for (const u32 count : buckets) {
        NX_CHECK(count > 850 && count < 1150);
    }
}

NX_TEST(Hash, CoversIntegersEnumsPointersAndSeeds) {
    NX_CHECK(HashValue(i32{-1}) == HashInteger(~u64{0}));
    NX_CHECK(HashValue(Color::Green) == HashInteger(1));
    NX_CHECK(HashValue(u8{7}) == HashValue(u64{7}));
    const i32 value = 0;
    NX_CHECK(HashValue(&value) == HashValue(&value));
    NX_CHECK(HashBytes("abc", 3, 1) != HashBytes("abc", 3, 2));
    NX_CHECK(HashValue(StringView("abc")) == HashText("abc"));
}
