#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/Sort.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Test/Test.h>

#include "ContainerTestTypes.h"

using namespace Nexyl;
using namespace Nexyl::Containers;
using Nexyl::Containers::Tests::Counted;
using Nexyl::Containers::Tests::TestRandom;

namespace {

bool Ascending(const u32& first, const u32& second) {
    return first < second;
}

// Sorts with each input shape and checks the result is ordered and a permutation of the input.
void CheckSortsShape(Memory::GuardAllocator& allocator, usize size, u32 shape) {
    Array<u32> values(allocator);
    TestRandom random(size * 7 + shape);
    u64 checksum = 0;
    for (usize index = 0; index < size; ++index) {
        u32 value = 0;
        switch (shape) {
            case 0:
                value = random.Below(1'000'000);
                break;
            case 1:
                value = static_cast<u32>(index);
                break;
            case 2:
                value = static_cast<u32>(size - index);
                break;
            case 3:
                value = 42;
                break;
            default:
                value = random.Below(4);
                break;
        }
        values.Push(value);
        checksum += value * u64{2654435761};
    }
    Sort(values);
    NX_CHECK(IsSorted(Span<const u32>(values), Ascending));
    u64 after = 0;
    for (const u32 value : values) {
        after += value * u64{2654435761};
    }
    NX_CHECK(after == checksum);
}

} // namespace

NX_TEST(Sort, OrdersEveryInputShape) {
    Memory::GuardAllocator allocator;
    const usize sizes[] = {0, 1, 2, 15, 16, 17, 100, 1000, 5000};
    for (const usize size : sizes) {
        for (u32 shape = 0; shape < 5; ++shape) {
            CheckSortsShape(allocator, size, shape);
        }
    }
}

NX_TEST(Sort, UsesTheComparisonAndMovesElementsOnly) {
    Memory::GuardAllocator allocator;
    {
        Array<Counted> values(allocator);
        for (i32 value = 0; value < 300; ++value) {
            values.Emplace((value * 37) % 300);
        }
        const i32 alive = Counted::s_alive;
        Sort(values, [](const Counted& first, const Counted& second) { return first.value > second.value; });
        NX_CHECK(Counted::s_alive == alive);
        for (usize index = 0; index < values.Size(); ++index) {
            NX_CHECK(values[index].value == static_cast<i32>(299 - index));
        }
    }
    NX_CHECK(Counted::s_alive == 0);
}

NX_TEST(Sort, FindsInSortedRanges) {
    const u32 values[] = {1, 3, 3, 5, 9};
    const Span<const u32> sorted(values);
    NX_CHECK(LowerBound(sorted, 3U, Ascending) == 1);
    NX_CHECK(LowerBound(sorted, 4U, Ascending) == 3);
    NX_CHECK(LowerBound(sorted, 0U, Ascending) == 0);
    NX_CHECK(LowerBound(sorted, 10U, Ascending) == 5);
    NX_CHECK(BinarySearch(sorted, 5U).GetValue() == 3);
    NX_CHECK(!BinarySearch(sorted, 4U).HasValue());
    NX_CHECK(!BinarySearch(Span<const u32>(), 4U).HasValue());
}
