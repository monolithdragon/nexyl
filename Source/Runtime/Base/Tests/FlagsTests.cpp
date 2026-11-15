#include <Nexyl/Base/Flags.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

namespace {

enum class Option : u8 {
    First,
    Second,
    Last = 63,
};

constexpr Flags<Option> kBoth{Option::First, Option::Second};

} // namespace

static_assert(Flags<Option>().IsEmpty());
static_assert(kBoth.Has(Option::First) && kBoth.Has(Option::Second) && !kBoth.Has(Option::Last));
static_assert(kBoth.GetBits() == 0b11U);
static_assert(Flags<Option>{Option::Last}.GetBits() == (u64{1} << 63U));
static_assert((kBoth & Flags<Option>{Option::Second}) == Flags<Option>{Option::Second});
static_assert((Flags<Option>{Option::First} | Flags<Option>{Option::Last}).Has(Option::Last));
static_assert(Flags<Option>::FromBits(0b10U) == Flags<Option>{Option::Second});

NX_TEST(Flags, SetsAndClearsValues) {
    Flags<Option> flags;
    flags.Set(Option::Second);
    flags.Set(Option::Second);
    NX_CHECK(flags.Has(Option::Second));
    NX_CHECK(!flags.Has(Option::First));
    flags.Clear(Option::Second);
    NX_CHECK(flags.IsEmpty());
}
