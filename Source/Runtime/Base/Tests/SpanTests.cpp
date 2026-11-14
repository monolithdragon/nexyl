#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

namespace {

/// A minimal contiguous container, standing in for Array until Containers exists.
struct FixedBuffer {
    i32 items[3] = {4, 5, 6};

    [[nodiscard]] i32* Data() {
        return items;
    }
    [[nodiscard]] usize Size() const {
        return 3;
    }
};

i32 Sum(Span<const i32> values) {
    i32 total = 0;
    for (const i32 value : values) {
        total += value;
    }
    return total;
}

} // namespace

NX_TEST(Span, ViewsAnArray) {
    i32 values[] = {1, 2, 3, 4, 5};
    const Span<i32> all(values);
    NX_CHECK(all.Size() == 5);
    NX_CHECK(all.SizeInBytes() == 5 * sizeof(i32));
    NX_CHECK(!all.IsEmpty());
    NX_CHECK(Sum(all) == 15);

    all[0] = 10;
    NX_CHECK(values[0] == 10);
}

NX_TEST(Span, SlicesWithinBounds) {
    const i32 values[] = {1, 2, 3, 4, 5};
    const Span<const i32> all(values);
    NX_CHECK(Sum(all.First(2)) == 3);
    NX_CHECK(Sum(all.Last(2)) == 9);
    NX_CHECK(Sum(all.Subspan(1, 3)) == 9);
    NX_CHECK(Sum(all.Subspan(4)) == 5);
    NX_CHECK(all.Subspan(5).IsEmpty());
}

NX_TEST(Span, ViewsAContiguousContainer) {
    FixedBuffer buffer;
    NX_CHECK(Sum(buffer) == 15);
}

NX_TEST(Span, DefaultIsEmpty) {
    const Span<i32> empty;
    NX_CHECK(empty.IsEmpty());
    NX_CHECK(empty.Data() == nullptr);
    NX_CHECK(Sum(empty) == 0);
}
