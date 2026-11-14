#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/Handle.h>
#include <Nexyl/Containers/HandlePool.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Test/Test.h>

#include "ContainerTestTypes.h"

using namespace Nexyl;
using namespace Nexyl::Containers;
using Nexyl::Containers::Tests::Counted;

NX_TEST(HandlePool, DetectsStaleHandles) {
    Memory::GuardAllocator allocator;
    {
        HandlePool<Counted> pool(allocator);
        const Handle<Counted> first = pool.Add(1);
        const Handle<Counted> second = pool.Add(2);
        NX_CHECK(!first.IsNull() && first != second);
        NX_CHECK(pool.Get(first)->value == 1 && pool.Get(second)->value == 2);
        NX_CHECK(pool.Size() == 2 && Counted::s_alive == 2);

        NX_CHECK(pool.Remove(first));
        NX_CHECK(!pool.Remove(first)); // Stale now.
        NX_CHECK(pool.Get(first) == nullptr && !pool.Contains(first));
        NX_CHECK(Counted::s_alive == 1);

        // The freed slot is reused with a new generation, so the old handle stays stale.
        const Handle<Counted> reused = pool.Add(3);
        NX_CHECK(reused.index == first.index && reused.generation != first.generation);
        NX_CHECK(pool.Get(first) == nullptr && pool.Get(reused)->value == 3);

        NX_CHECK(pool.Get(Handle<Counted>{}) == nullptr);
        NX_CHECK(pool.Get(Handle<Counted>{500, 1}) == nullptr);
    }
    NX_CHECK(Counted::s_alive == 0);
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(HandlePool, KeepsObjectsInPlaceAndIteratesInSlotOrder) {
    Memory::GuardAllocator allocator;
    {
        HandlePool<Counted> pool(allocator);
        Handle<Counted> handles[200] = {};
        handles[1] = pool.Add(1);
        const Counted* const early = pool.Get(handles[1]);
        handles[0] = pool.Add(0);
        for (u32 index = 2; index < 200; ++index) {
            handles[index] = pool.Add(static_cast<i32>(index));
        }
        // Growing past several chunks does not move existing objects.
        NX_CHECK(pool.Get(handles[1]) == early);
        for (usize index = 0; index < 200; index += 2) {
            NX_CHECK(pool.Remove(handles[index]));
        }
        NX_CHECK(pool.Get(handles[1]) == early && pool.Get(handles[0]) == nullptr);

        i32 previous = -1;
        usize visited = 0;
        for (const auto [handle, value] : pool) {
            NX_CHECK(value.value > previous && value.value % 2 == 1);
            NX_CHECK(pool.Get(handle) == &value);
            previous = value.value;
            ++visited;
        }
        NX_CHECK(visited == 100 && pool.Size() == 100);

        HashMap<Handle<Counted>, u32> names(allocator);
        names.Set(handles[1], 1);
        NX_CHECK(*names.Find(handles[1]) == 1);

        HandlePool<Counted> copy = pool.Clone(allocator);
        NX_CHECK(copy.Get(handles[3])->value == 3 && copy.Get(handles[2]) == nullptr);

        pool.Clear();
        NX_CHECK(pool.IsEmpty() && pool.Get(handles[1]) == nullptr);
        const Handle<Counted> after = pool.Add(9);
        NX_CHECK(after.index == 0 && pool.Get(after)->value == 9);

        HandlePool<Counted> moved(Nexyl::Move(copy));
        NX_CHECK(copy.IsEmpty() && moved.Get(handles[5])->value == 5);
    }
    NX_CHECK(Counted::s_alive == 0);
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}
