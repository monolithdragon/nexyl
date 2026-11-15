#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Test/Fuzz.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

namespace {

constexpr u8 kFirst[] = {'a', 'b', 'c', 'd'};
constexpr u8 kSecond[] = {0x10, 0x20, 0x30};
constexpr Span<const u8> kCorpus[] = {Span<const u8>(kFirst), Span<const u8>(kSecond)};

struct Summary {
    u64 hash = 0xCBF2'9CE4'8422'2325ULL;
    u32 runs = 0;
    usize largest = 0;
    bool sawToken = false;
    bool corpusFirst = true;
};

void Record(Summary& summary, Span<const u8> input) {
    if (summary.runs < 2) {
        const Span<const u8> expected = kCorpus[summary.runs];
        bool same = input.Size() == expected.Size();
        for (usize index = 0; same && index < input.Size(); ++index) {
            same = input[index] == expected[index];
        }
        summary.corpusFirst = summary.corpusFirst && same;
    }
    for (const u8 byte : input) {
        summary.hash = (summary.hash ^ byte) * 0x100'0000'01B3ULL;
    }
    summary.hash = (summary.hash ^ 0xFFU) * 0x100'0000'01B3ULL;
    for (usize index = 0; index + 2 < input.Size(); ++index) {
        summary.sawToken =
            summary.sawToken || (input[index] == 'x' && input[index + 1] == 'y' && input[index + 2] == 'z');
    }
    summary.largest = input.Size() > summary.largest ? input.Size() : summary.largest;
    ++summary.runs;
}

Summary Run(u64 seed) {
    constexpr Span<const char> kDictionary[] = {Span<const char>("xyz", 3)};
    u8 scratch[32] = {};
    Summary summary;
    Test::FuzzOptions options;
    options.seed = seed;
    options.iterations = 500;
    options.dictionary = Span<const Span<const char>>(kDictionary);
    const u32 runs = Test::Fuzz(Span<const Span<const u8>>(kCorpus), scratch, options,
                                [&summary](Span<const u8> input) { Record(summary, input); });
    summary.runs = runs == summary.runs ? summary.runs : 0;
    return summary;
}

} // namespace

NX_TEST(Fuzz, PassesTheCorpusThenMutations) {
    const Summary summary = Run(7);
    NX_CHECK(summary.runs == 502);
    NX_CHECK(summary.corpusFirst);
    NX_CHECK(summary.largest <= 32);
    NX_CHECK(summary.sawToken);
}

NX_TEST(Fuzz, IsDeterministic) {
    NX_CHECK(Run(7).hash == Run(7).hash);
    NX_CHECK(Run(7).hash != Run(8).hash);
}
