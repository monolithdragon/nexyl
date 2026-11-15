#include <Nexyl/Base/InitializerList.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Containers/HashSet.h>
#include <Nexyl/Containers/String.h>
#include <Nexyl/Containers/StringView.h>
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
using Nexyl::Containers::Tests::TestRandom;

NX_TEST(HashMap, InsertsFindsAndReplaces) {
    Memory::GuardAllocator allocator;
    {
        HashMap<u32, u32> map(allocator);
        NX_CHECK(map.Find(1U) == nullptr && !map.Remove(1U));
        NX_CHECK(map.Insert(1, 10));
        NX_CHECK(!map.Insert(1, 99)); // Present: unchanged.
        NX_CHECK(*map.Find(1U) == 10);
        NX_CHECK(map.Set(1, 11) == 11 && *map.Find(1U) == 11);
        map.FindOrAdd(2) += 5;
        map.FindOrAdd(2) += 5;
        NX_CHECK(*map.Find(2U) == 10 && map.Size() == 2);
        NX_CHECK(map.Contains(2U) && !map.Contains(3U));

        const HashMap<u32, u32>& constant = map;
        NX_CHECK(*constant.Find(1U) == 11 && constant.GetEntry(1).key == 2);
        map.Clear();
        NX_CHECK(map.IsEmpty() && !map.Contains(1U));
        map.Set(7, 70);
        NX_CHECK(*map.Find(7U) == 70);
    }
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(HashMap, IteratesInInsertionOrderAndRemovesDeterministically) {
    Memory::GuardAllocator allocator;
    {
        HashMap<u32, u32> map(allocator);
        for (u32 key = 100; key < 110; ++key) {
            map.Set(key, key * 2);
        }
        u32 expected = 100;
        for (const auto [key, value] : map) {
            NX_CHECK(key == expected && value == key * 2);
            ++expected;
        }

        // Remove moves the last entry into the gap; RemoveOrdered closes it.
        NX_CHECK(map.Remove(102U));
        NX_CHECK(map.GetEntry(2).key == 109 && map.Size() == 9);
        NX_CHECK(map.RemoveOrdered(100U));
        NX_CHECK(map.GetEntry(0).key == 101 && map.GetEntry(1).key == 109 && map.GetEntry(2).key == 103);
        for (const u32 key : {101U, 103U, 104U, 105U, 106U, 107U, 108U, 109U}) {
            NX_CHECK(map.Contains(key));
        }
        for (auto entry : map) {
            entry.value = 0;
        }
        NX_CHECK(*map.Find(105U) == 0);
    }
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(HashMap, UsesStringKeysAndViewLookups) {
    Memory::GuardAllocator allocator;
    {
        HashMap<String, Counted> map(allocator);
        map.Set(String(allocator, "alpha"), Counted(1));
        map.Set(String(allocator, "beta"), Counted(2));
        NX_CHECK(map.Find(StringView("alpha"))->value == 1);
        NX_CHECK(map.Find(StringView("gamma")) == nullptr);
        NX_CHECK(map.Remove(StringView("alpha")) && map.Size() == 1);
        NX_CHECK(Counted::s_alive == 1);

        HashMap<String, Counted> copy = map.Clone();
        NX_CHECK(copy.Find(StringView("beta"))->value == 2 && Counted::s_alive == 2);
        const HashMap<String, Counted> moved(Nexyl::Move(copy));
        NX_CHECK(copy.IsEmpty() && moved.Size() == 1);
    }
    NX_CHECK(Counted::s_alive == 0);
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(HashMap, MatchesAReferenceModelUnderRandomOperations) {
    Memory::PageAllocator pages;
    Memory::Heap heap(pages);
    Memory::TrackingAllocator tracked(heap, Memory::MemoryTag::Make(ModuleId::Memory, 10));
    {
        struct ModelEntry {
            u32 key = 0;
            u32 value = 0;
        };
        // The model keeps entries in an array and searches linearly; it applies the same removal rules.
        Array<ModelEntry> model(tracked);
        HashMap<u32, u32> map(tracked);
        TestRandom random(2024);
        const auto findInModel = [&model](u32 key) -> usize {
            for (usize index = 0; index < model.Size(); ++index) {
                if (model[index].key == key) {
                    return index;
                }
            }
            return model.Size();
        };

        for (u32 step = 0; step < 30'000; ++step) {
            const u32 key = random.Below(600);
            const u32 action = random.Below(10);
            const usize position = findInModel(key);
            const bool present = position < model.Size();
            if (action < 5) {
                map.Set(key, step);
                if (present) {
                    model[position].value = step;
                } else {
                    model.Push(ModelEntry{key, step});
                }
            } else if (action < 8) {
                NX_CHECK(map.Remove(key) == present);
                if (present) {
                    model.RemoveAtSwap(position);
                }
            } else if (action < 9) {
                NX_CHECK(map.RemoveOrdered(key) == present);
                if (present) {
                    model.RemoveAt(position);
                }
            } else {
                const u32* const found = map.Find(key);
                NX_CHECK((found != nullptr) == present);
                NX_CHECK(found == nullptr || *found == model[position].value);
            }

            if (step % 1000 == 0 || step == 29'999) {
                NX_REQUIRE(map.Size() == model.Size());
                for (usize index = 0; index < model.Size(); ++index) {
                    NX_CHECK(map.GetEntry(index).key == model[index].key);
                    NX_CHECK(map.GetEntry(index).value == model[index].value);
                }
            }
        }
    }
    NX_CHECK(tracked.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(HashSet, AddsAndRemovesKeysInOrder) {
    Memory::GuardAllocator allocator;
    {
        HashSet<u32> set(allocator);
        NX_CHECK(set.Add(3) && set.Add(1) && set.Add(2) && !set.Add(3));
        NX_CHECK(set.Size() == 3 && set.Contains(1U) && !set.Contains(4U));
        u32 order[3] = {};
        usize count = 0;
        for (const u32 key : set) {
            order[count++] = key;
        }
        NX_CHECK(order[0] == 3 && order[1] == 1 && order[2] == 2);
        NX_CHECK(set.Remove(3U) && set.GetKey(0) == 2);
        NX_CHECK(set.RemoveOrdered(2U) && set.GetKey(0) == 1);

        const HashSet<u32> copy = set.Clone();
        NX_CHECK(copy.Size() == 1 && copy.Contains(1U));
        set.Clear();
        NX_CHECK(set.IsEmpty());
    }
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}
