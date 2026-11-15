#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Memory/Alignment.h>
#include <Nexyl/Memory/Allocation.h>
#include <Nexyl/Memory/ArenaAllocator.h>
#include <Nexyl/Memory/FreeListAllocator.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Memory/Heap.h>
#include <Nexyl/Memory/LinearAllocator.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryTag.h>
#include <Nexyl/Memory/PageAllocator.h>
#include <Nexyl/Memory/PoolAllocator.h>
#include <Nexyl/Memory/StackAllocator.h>
#include <Nexyl/Memory/TrackingAllocator.h>
#include <Nexyl/Platform/SystemInfo.h>
#include <Nexyl/Platform/Thread.h>
#include <Nexyl/Test/Test.h>

#include "AllocatorChecks.h"

using namespace Nexyl;
using namespace Nexyl::Memory;
using Nexyl::Memory::Tests::CheckContract;
using Nexyl::Memory::Tests::Fill;
using Nexyl::Memory::Tests::HasPattern;
using Nexyl::Memory::Tests::TestRandom;

namespace {

bool IsCapacityExceeded(const Result<void*, Error>& result) {
    return result.IsError() && result.GetError().Is(MemoryError::CapacityExceeded);
}

} // namespace

NX_TEST(PageAllocator, MeetsTheContractAndReturnsPages) {
    PageAllocator pages;
    CheckContract(pages, 3 * pages.GetPageSize(), pages.GetAllocationGranularity());
    NX_CHECK(pages.GetStatistics().liveAllocationCount == 0);
    NX_CHECK(pages.GetStatistics().bytesReserved == 0);

    void* const block = Allocate(pages, 1);
    NX_CHECK(pages.GetStatistics().bytesReserved == pages.GetPageSize());
    pages.Deallocate(block, 1);
    NX_CHECK(pages.GetStatistics().peakBytesInUse > 0);
}

NX_TEST(LinearAllocator, BumpsAndResets) {
    alignas(64) u8 storage[8192] = {};
    LinearAllocator linear(storage);
    NX_CHECK(linear.GetCapacity() == sizeof(storage));
    CheckContract(linear, 64, 64);

    linear.Reset();
    NX_CHECK(linear.GetUsedBytes() == 0 && linear.GetStatistics().liveAllocationCount == 0);
    void* const first = Allocate(linear, 100, 16);
    NX_CHECK(first == storage);
    // The most recent block grows in place.
    NX_CHECK(Reallocate(linear, first, 100, 200, 16) == first);
    NX_CHECK(linear.GetUsedBytes() == 200);

    NX_CHECK(IsCapacityExceeded(linear.TryAllocate(sizeof(storage), 16)));
    NX_CHECK(!linear.Owns(storage + sizeof(storage)));
    linear.Reset();
    NX_CHECK(Allocate(linear, sizeof(storage), 16) == storage);
}

NX_TEST(StackAllocator, RewindsToMarkersAndPopsTheTop) {
    alignas(16) u8 storage[4096] = {};
    StackAllocator stack(storage);
    CheckContract(stack, 32, 32);

    const StackMarker start = stack.GetMarker();
    const void* const outer = Allocate(stack, 64);
    const StackMarker middle = stack.GetMarker();
    static_cast<void>(Allocate(stack, 128));
    void* const top = Allocate(stack, 32);
    NX_CHECK(stack.GetStatistics().liveAllocationCount == middle.liveAllocationCount + 2);

    // Releasing the top gives its memory back at once.
    const usize usedWithTop = stack.GetUsedBytes();
    stack.Deallocate(top, 32);
    NX_CHECK(stack.GetUsedBytes() < usedWithTop);

    stack.FreeToMarker(middle);
    NX_CHECK(stack.GetUsedBytes() == middle.offset);
    NX_CHECK(stack.GetStatistics().liveAllocationCount == middle.liveAllocationCount);
    NX_CHECK(stack.Owns(outer));

    stack.FreeToMarker(start);
    NX_CHECK(stack.GetUsedBytes() == start.offset);
    NX_CHECK(stack.GetStatistics().bytesInUse == start.bytesInUse);
}

NX_TEST(ArenaAllocator, GrowsInBlocksAndReleasesInBulk) {
    PageAllocator pages;
    {
        ArenaAllocator arena(pages, 4096);
        CheckContract(arena, 512, 256);

        // A request larger than the block size gets a block of its own.
        const void* const big = Allocate(arena, 10000);
        NX_CHECK(arena.Owns(big));
        NX_CHECK(arena.GetStatistics().bytesReserved >= 10000);

        const usize reservedBefore = arena.GetStatistics().bytesReserved;
        arena.Reset();
        NX_CHECK(arena.GetStatistics().liveAllocationCount == 0);
        NX_CHECK(arena.GetStatistics().bytesReserved < reservedBefore);
        NX_CHECK(!arena.Owns(big) || arena.GetStatistics().bytesReserved >= 10000);

        const void* const after = Allocate(arena, 64);
        NX_CHECK(arena.Owns(after));
        arena.Release();
        NX_CHECK(arena.GetStatistics().bytesReserved == 0);
        NX_CHECK(!arena.Owns(after));
    }
    NX_CHECK(pages.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(PoolAllocator, ReusesFixedSizeBlocks) {
    PageAllocator pages;
    {
        PoolAllocator pool(pages, 48, 16, 4);
        NX_CHECK(pool.GetBlockSize() == 48);

        struct PoolBlock {
            u8* address = nullptr;
        };
        PoolBlock blocks[10] = {};
        for (usize index = 0; index < 10; ++index) {
            blocks[index].address = static_cast<u8*>(Allocate(pool, 40));
            NX_CHECK(IsAligned(blocks[index].address, 16));
            NX_CHECK(pool.Owns(blocks[index].address));
            Fill(blocks[index].address, 40, static_cast<u8>(index));
        }
        // Ten blocks need three chunks of four.
        NX_CHECK(pages.GetStatistics().liveAllocationCount == 3);
        for (usize index = 0; index < 10; ++index) {
            NX_CHECK(HasPattern(blocks[index].address, 40, static_cast<u8>(index)));
        }

        // A released block is the next one handed out.
        pool.Deallocate(blocks[3].address, 40);
        NX_CHECK(Allocate(pool, 8) == blocks[3].address);
        NX_CHECK(!pool.Owns(blocks[0].address + 8));

        NX_CHECK(IsCapacityExceeded(pool.TryAllocate(49, 16)));
        NX_CHECK(IsCapacityExceeded(pool.TryAllocate(8, 64)));
        NX_CHECK(Reallocate(pool, blocks[0].address, 40, 48) == blocks[0].address);
        NX_CHECK(IsCapacityExceeded(pool.TryReallocate(blocks[0].address, 48, 49, 16)));

        pool.Deallocate(blocks[0].address, 48);
        pool.Deallocate(blocks[3].address, 8);
        for (usize index = 1; index < 10; ++index) {
            if (index != 3) {
                pool.Deallocate(blocks[index].address, 40);
            }
        }
        NX_CHECK(pool.GetStatistics().liveAllocationCount == 0);
    }
    NX_CHECK(pages.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(FreeListAllocator, MeetsTheContract) {
    PageAllocator pages;
    constexpr usize kPoolSize = usize{256} * 1024;
    void* const memory = Allocate(pages, kPoolSize);
    {
        FreeListAllocator freeList(Span<u8>(static_cast<u8*>(memory), kPoolSize));
        CheckContract(freeList, 4096, 4096);
        NX_CHECK(freeList.GetStatistics().liveAllocationCount == 0);
        NX_CHECK(freeList.GetStatistics().bytesReserved == kPoolSize);
    }
    pages.Deallocate(memory, kPoolSize);
}

NX_TEST(FreeListAllocator, SurvivesRandomTrafficAndMergesEverythingBack) {
    PageAllocator pages;
    constexpr usize kPoolSize = usize{1} << 20U;
    void* const memory = Allocate(pages, kPoolSize);
    {
        FreeListAllocator freeList(Span<u8>(static_cast<u8*>(memory), kPoolSize));

        struct Live {
            void* block = nullptr;
            usize size = 0;
            usize alignment = 0;
            u8 seed = 0;
        };
        constexpr usize kSlots = 128;
        Live live[kSlots] = {};
        TestRandom random(12345);
        u32 failures = 0;

        for (u32 step = 0; step < 20'000; ++step) {
            Live& slot = live[random.Between(0, kSlots - 1)];
            const usize action = random.Between(0, 9);
            if (slot.block == nullptr) {
                slot.size = random.Between(1, action < 8 ? 512 : 16384);
                slot.alignment = usize{16} << random.Between(0, 4);
                const Result<void*, Error> allocated = freeList.TryAllocate(slot.size, slot.alignment);
                if (allocated.IsError()) {
                    ++failures;
                    continue;
                }
                slot.block = allocated.GetValue();
                slot.seed = static_cast<u8>(step);
                NX_CHECK(IsAligned(slot.block, slot.alignment));
                Fill(slot.block, slot.size, slot.seed);
            } else if (action < 3) {
                const usize newSize = random.Between(1, 4096);
                const Result<void*, Error> resized =
                    freeList.TryReallocate(slot.block, slot.size, newSize, slot.alignment);
                if (resized.IsError()) {
                    ++failures;
                    continue;
                }
                NX_CHECK(HasPattern(resized.GetValue(), newSize < slot.size ? newSize : slot.size, slot.seed));
                slot.block = resized.GetValue();
                slot.size = newSize;
                NX_CHECK(IsAligned(slot.block, slot.alignment));
                Fill(slot.block, slot.size, slot.seed);
            } else {
                NX_CHECK(HasPattern(slot.block, slot.size, slot.seed));
                freeList.Deallocate(slot.block, slot.size);
                slot.block = nullptr;
            }
        }
        // 128 live blocks of at most 16 KiB fit easily in 1 MiB; failures would mean lost or fragmented memory.
        NX_CHECK(failures == 0);

        for (const Live& slot : live) {
            if (slot.block != nullptr) {
                NX_CHECK(HasPattern(slot.block, slot.size, slot.seed));
                freeList.Deallocate(slot.block, slot.size);
            }
        }
        NX_CHECK(freeList.GetStatistics().liveAllocationCount == 0);
        NX_CHECK(freeList.GetStatistics().bytesInUse == 0);

        // Everything merged back into one block: nine tenths of the pool fit in a single allocation.
        const usize large = kPoolSize / 10 * 9;
        void* const whole = Allocate(freeList, large);
        freeList.Deallocate(whole, large);
    }
    pages.Deallocate(memory, kPoolSize);
}

NX_TEST(FreeListAllocator, GrowsInPlaceAndUsesSeveralPools) {
    PageAllocator pages;
    constexpr usize kPoolSize = 4096;
    void* const first = Allocate(pages, kPoolSize);
    void* const second = Allocate(pages, kPoolSize);
    {
        FreeListAllocator freeList;
        NX_CHECK(IsCapacityExceeded(freeList.TryAllocate(16, 16)));
        freeList.AddPool(Span<u8>(static_cast<u8*>(first), kPoolSize));

        void* const block = Allocate(freeList, 100);
        // The rest of the pool follows the block, so it grows without moving.
        NX_CHECK(Reallocate(freeList, block, 100, 2000) == block);
        NX_CHECK(IsCapacityExceeded(freeList.TryAllocate(3000, 16)));

        freeList.AddPool(Span<u8>(static_cast<u8*>(second), kPoolSize));
        void* const other = Allocate(freeList, 3000);
        NX_CHECK(freeList.Owns(other) && freeList.Owns(block));

        freeList.Deallocate(block, 2000);
        freeList.Deallocate(other, 3000);
    }
    pages.Deallocate(first, kPoolSize);
    pages.Deallocate(second, kPoolSize);
}

NX_TEST(Heap, MeetsTheContractAcrossSizeRanges) {
    PageAllocator pages;
    {
        HeapDesc desc;
        desc.poolSize = usize{64} * 1024;
        desc.largeBlockThreshold = usize{32} * 1024;
        Heap heap(pages, desc);
        CheckContract(heap, 2048, 256);
        // Sizes on both sides of the large-block threshold, and pool growth.
        CheckContract(heap, usize{100} * 1024, 4096);

        void* const large = Allocate(heap, usize{40} * 1024, 4096);
        NX_CHECK(heap.Owns(large) && IsAligned(large, 4096));
        void* const shrunk = Reallocate(heap, large, usize{40} * 1024, 1000, 4096);
        NX_CHECK(heap.Owns(shrunk) && IsAligned(shrunk, 4096));
        heap.Deallocate(shrunk, 1000);

        NX_CHECK(heap.GetStatistics().liveAllocationCount == 0);
        NX_CHECK(heap.GetStatistics().bytesReserved > 0);
    }
    NX_CHECK(pages.GetStatistics().liveAllocationCount == 0);
}

namespace {

struct HeapWorker {
    Heap* heap = nullptr;
    u64 seed = 0;
    u32 corrupted = 0;
};

void HammerHeap(void* argument) {
    auto* const worker = static_cast<HeapWorker*>(argument);
    struct Live {
        void* block = nullptr;
        usize size = 0;
    };
    Live live[32] = {};
    TestRandom random(worker->seed);
    for (u32 step = 0; step < 5000; ++step) {
        Live& slot = live[random.Between(0, 31)];
        if (slot.block == nullptr) {
            slot.size = random.Between(1, 3000);
            slot.block = Allocate(*worker->heap, slot.size);
            Fill(slot.block, slot.size, static_cast<u8>(worker->seed));
        } else {
            if (!HasPattern(slot.block, slot.size, static_cast<u8>(worker->seed))) {
                ++worker->corrupted;
            }
            worker->heap->Deallocate(slot.block, slot.size);
            slot.block = nullptr;
        }
    }
    for (const Live& slot : live) {
        worker->heap->Deallocate(slot.block, slot.size);
    }
}

} // namespace

NX_TEST(Heap, ServesManyThreadsAtOnce) {
    PageAllocator pages;
    {
        Heap heap(pages);
        constexpr u32 kThreads = 4;
        HeapWorker workers[kThreads] = {};
        Platform::Thread threads[kThreads];
        for (u32 index = 0; index < kThreads; ++index) {
            workers[index].heap = &heap;
            workers[index].seed = 100 + index;
            Result<Platform::Thread, Error> created =
                Platform::Thread::Create(Platform::ThreadDesc{.function = &HammerHeap, .argument = &workers[index]});
            NX_REQUIRE(created.IsOk());
            threads[index] = Nexyl::Move(created).TakeValue();
        }
        for (Platform::Thread& thread : threads) {
            thread.Join();
        }
        for (const HeapWorker& worker : workers) {
            NX_CHECK(worker.corrupted == 0);
        }
        NX_CHECK(heap.GetStatistics().liveAllocationCount == 0);
        NX_CHECK(heap.GetStatistics().totalAllocationCount >= u64{kThreads} * 2000);
    }
    NX_CHECK(pages.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(TrackingAllocator, CountsOneOwnersBlocks) {
    PageAllocator pages;
    Heap heap(pages);
    const MemoryTag tag = MemoryTag::Make(ModuleId::Memory, 2);
    TrackingAllocator first(heap, tag);
    TrackingAllocator second(heap, MemoryTag::Make(ModuleId::Memory, 3));
    NX_CHECK(first.GetTag() == tag && &first.GetInner() == &heap);

    void* const a = Allocate(first, 100);
    void* const b = Allocate(second, 300);
    NX_CHECK(first.GetStatistics().bytesInUse == 100 && second.GetStatistics().bytesInUse == 300);
    NX_CHECK(heap.GetStatistics().bytesInUse == 400);

    void* const grown = Reallocate(first, a, 100, 150);
    NX_CHECK(first.GetStatistics().bytesInUse == 150 && first.GetStatistics().peakBytesInUse == 150);
    NX_CHECK(first.Owns(grown));

    first.Deallocate(grown, 150);
    second.Deallocate(b, 300);
    NX_CHECK(first.GetStatistics().liveAllocationCount == 0 && second.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(GuardAllocator, PlacesEveryBlockAgainstAGuardPage) {
    GuardAllocator guarded;
    CheckContract(guarded, 5000, 64);

    const usize pageSize = Platform::GetSystemInfo().pageSize;
    const usize sizes[] = {1, 17, 4096, 10000};
    const usize alignments[] = {1, 16, 64};
    for (const usize size : sizes) {
        for (const usize alignment : alignments) {
            u8* const block = static_cast<u8*>(Allocate(guarded, size, alignment));
            NX_CHECK(IsAligned(block, alignment));
            // The block ends less than one alignment step before a page boundary, where the guard page starts.
            const usize end = ToAddress(block) + size;
            NX_CHECK(AlignUp(end, pageSize) - end < alignment);
            Fill(block, size, 3);
            NX_CHECK(HasPattern(block, size, 3));
            guarded.Deallocate(block, size);
        }
    }
    NX_CHECK(guarded.GetStatistics().liveAllocationCount == 0);
    NX_CHECK(guarded.GetStatistics().bytesReserved == 0);
}
