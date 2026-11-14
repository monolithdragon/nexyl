#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Memory/Heap.h>
#include <Nexyl/Memory/MemoryTag.h>
#include <Nexyl/Memory/PageAllocator.h>
#include <Nexyl/Memory/TrackingAllocator.h>
#include <Nexyl/Test/Test.h>

#include "ContainerTestTypes.h"

using namespace Nexyl;
using namespace Nexyl::Containers;
using Nexyl::Containers::Tests::Counted;

namespace {

u32 Sum(Span<const u32> values) {
    u32 total = 0;
    for (const u32 value : values) {
        total += value;
    }
    return total;
}

} // namespace

NX_TEST(Array, GrowsPushesAndPops) {
    Memory::GuardAllocator allocator;
    {
        Array<u32> values(allocator);
        NX_CHECK(values.IsEmpty() && values.Data() == nullptr && values.GetAllocator() == &allocator);
        for (u32 index = 0; index < 100; ++index) {
            NX_CHECK(values.Push(index) == index);
        }
        NX_CHECK(values.Size() == 100 && values.Capacity() >= 100);
        NX_CHECK(values.Front() == 0 && values.Back() == 99 && values[50] == 50);
        NX_CHECK(Sum(values) == 4950);
        NX_CHECK(values.Pop() == 99 && values.Size() == 99);

        values.Clear();
        NX_CHECK(values.IsEmpty() && values.Capacity() >= 100);
        values.ShrinkToFit();
        NX_CHECK(values.Capacity() == 0 && values.Data() == nullptr);
    }
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(Array, ConstructsMovesAndDestroysEveryElementOnce) {
    Memory::GuardAllocator allocator;
    {
        Array<Counted> values(allocator, {Counted(1), Counted(2), Counted(3)});
        NX_CHECK(Counted::s_alive == 3);
        values.Emplace(4);
        values.Push(Counted(5));
        values.Resize(8);
        NX_CHECK(Counted::s_alive == 8 && values[7].value == 0);
        values.Resize(4);
        NX_CHECK(Counted::s_alive == 4);

        values.Insert(0, Counted(0));
        values.Insert(2, Counted(15));
        NX_CHECK(values.Size() == 6);
        const i32 expected[] = {0, 1, 15, 2, 3, 4};
        for (usize index = 0; index < values.Size(); ++index) {
            NX_CHECK(values[index].value == expected[index]);
        }

        values.RemoveAt(2);
        NX_CHECK(values[2].value == 2 && values.Size() == 5);
        values.RemoveAtSwap(0);
        NX_CHECK(values[0].value == 4 && values.Size() == 4);
        NX_CHECK(values.IndexOf(Counted(3)).GetValue() == 3);
        NX_CHECK(!values.Contains(Counted(42)));
        NX_CHECK(Counted::s_alive == 4);

        Array<Counted> moved(Nexyl::Move(values));
        NX_CHECK(values.IsEmpty() && moved.Size() == 4);
        values.Clear(); // The moved-from array is valid; it kept no allocator.
        NX_CHECK(values.GetAllocator() == nullptr);
        Array<Counted> copy = moved.Clone();
        NX_CHECK(copy == moved && Counted::s_alive == 8);

        moved = Nexyl::Move(copy);
        NX_CHECK(Counted::s_alive == 4);
    }
    NX_CHECK(Counted::s_alive == 0);
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(Array, AddsElementsOfItselfWhileGrowing) {
    Memory::GuardAllocator allocator;
    {
        Array<Counted> values(allocator);
        values.Push(Counted(7));
        // Each push reads an element that growing moves; the copy must be made before the move.
        for (u32 round = 0; round < 40; ++round) {
            values.Push(values[0]);
        }
        NX_CHECK(values.Size() == 41 && values.Back().value == 7);

        Array<u32> numbers(allocator, {1, 2, 3});
        for (u32 round = 0; round < 5; ++round) {
            numbers.Append(numbers);
        }
        NX_CHECK(numbers.Size() == 96 && Sum(numbers) == 6 * 32);

        numbers.Resize(200, numbers[0]);
        NX_CHECK(numbers.Back() == 1);
    }
    NX_CHECK(Counted::s_alive == 0);
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(Array, ReservesAndReportsFailure) {
    Memory::PageAllocator pages;
    Memory::Heap heap(pages);
    Memory::TrackingAllocator tracked(heap, Memory::MemoryTag::Make(ModuleId::Memory, 9));
    {
        Array<u64> values(tracked);
        NX_CHECK(values.TryReserve(1000).IsOk() && values.Capacity() == 1000);
        NX_CHECK(tracked.GetStatistics().liveAllocationCount == 1);
        // More than any allocator accepts: the array reports the failure and stays usable.
        NX_CHECK(values.TryReserve(~usize{0} / 16).IsError());
        NX_CHECK(values.Capacity() == 1000);
        values.Push(5);
        NX_CHECK(values[0] == 5);
    }
    NX_CHECK(tracked.GetStatistics().liveAllocationCount == 0);

    const Array<u32> empty;
    NX_CHECK(empty.Clone().GetAllocator() == nullptr && empty.begin() == empty.end());
}
