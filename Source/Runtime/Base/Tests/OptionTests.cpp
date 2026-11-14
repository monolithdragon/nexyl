#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Test/Test.h>

#include "TestTypes.h"

using namespace Nexyl;
using Nexyl::Tests::MoveOnly;
using Nexyl::Tests::Tracked;

static_assert(!Option<i32>().HasValue());
static_assert(Option<i32>(3).GetValue() == 3);
static_assert(Option<i32>(kNone).ValueOr(8) == 8);

namespace {

Option<i32> FindFirstEven(Span<const i32> values) {
    for (const i32 value : values) {
        if (value % 2 == 0) {
            return value;
        }
    }
    return kNone;
}

} // namespace

NX_TEST(Option, ReturnsValueOrNone) {
    const i32 values[] = {1, 3, 4, 6};
    const i32 odd[] = {1, 3};
    NX_CHECK(FindFirstEven(values).ValueOr(0) == 4);
    NX_CHECK(!FindFirstEven(odd).HasValue());
}

NX_TEST(Option, DestroysItsValueExactlyOnce) {
    {
        Option<Tracked> option(Tracked(1));
        NX_CHECK(Tracked::s_alive == 1);

        const Option<Tracked> copy(option);
        NX_CHECK(Tracked::s_alive == 2);
        NX_CHECK(copy.GetValue().value == 1);

        option.Emplace(5);
        NX_CHECK(Tracked::s_alive == 2);
        NX_CHECK(option.GetValue().value == 5);

        option.Reset();
        NX_CHECK(!option.HasValue());
        NX_CHECK(Tracked::s_alive == 1);

        option = copy;
        NX_CHECK(option.HasValue());
        NX_CHECK(option.GetValue().value == 1);
        NX_CHECK(Tracked::s_alive == 2);
    }
    NX_CHECK(Tracked::s_alive == 0);
}

NX_TEST(Option, SupportsMoveOnlyValues) {
    Option<MoveOnly> moveOnly(MoveOnly(3));
    const MoveOnly taken = Move(moveOnly).TakeValue();
    NX_CHECK(taken.value == 3);
}
