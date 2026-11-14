#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Memory/Alignment.h>
#include <Nexyl/Memory/Allocation.h>
#include <Nexyl/Memory/Heap.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Memory/MemoryTag.h>
#include <Nexyl/Memory/PageAllocator.h>
#include <Nexyl/Memory/TrackingAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Memory;

static_assert(ErrorEnum<MemoryError>);
static_assert(AlignUp(usize{0}, 16) == 0 && AlignUp(usize{1}, 16) == 16 && AlignUp(usize{32}, 16) == 32);
static_assert(AlignDown(usize{47}, 16) == 32);
static_assert(MemoryTag::Make(ModuleId::Memory, 7).GetModule() == ModuleId::Memory);
static_assert(MemoryTag::Make(ModuleId::Memory, 7).GetSubTag() == 7);
static_assert(MemoryTag{}.IsUntagged() && !MemoryTag::Make(ModuleId::Base).IsUntagged());

namespace {

struct Counted {
    static inline i32 s_alive = 0;
    i32 value = 0;

    explicit Counted(i32 initial) : value(initial) {
        ++s_alive;
    }
    Counted(const Counted&) = delete;
    Counted& operator=(const Counted&) = delete;
    Counted(Counted&&) = delete;
    Counted& operator=(Counted&&) = delete;
    ~Counted() {
        --s_alive;
    }
};

void IgnoreOutOfMemory(const OutOfMemoryInfo& /*info*/) {}

} // namespace

NX_TEST(MemoryOperations, CopyMoveSetAndCompare) {
    u8 source[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    u8 target[8] = {};
    Copy(target, source, sizeof(source));
    NX_CHECK(Compare(target, source, sizeof(source)) == 0);

    // Overlapping move to the right.
    Move(source + 2, source, 6);
    const u8 shifted[8] = {1, 2, 1, 2, 3, 4, 5, 6};
    NX_CHECK(Compare(source, shifted, 8) == 0);

    Set(target, 0xFF, 4);
    NX_CHECK(target[0] == 0xFF && target[3] == 0xFF && target[4] == 5);

    // The result is exactly -1, 0 or 1, and bytes compare as unsigned.
    const u8 low[2] = {1, 0x01};
    const u8 high[2] = {1, 0xF0};
    NX_CHECK(Compare(low, high, 2) == -1);
    NX_CHECK(Compare(high, low, 2) == 1);

    // Zero sizes accept null pointers.
    Copy(nullptr, nullptr, 0);
    Move(nullptr, nullptr, 0);
    Set(nullptr, 0, 0);
    NX_CHECK(Compare(nullptr, nullptr, 0) == 0);
}

NX_TEST(Alignment, PointersAndValues) {
    alignas(64) u8 buffer[128] = {};
    NX_CHECK(IsAligned(buffer, 64));
    NX_CHECK(!IsAligned(buffer + 1, 2));
    NX_CHECK(AlignUp(buffer + 1, 32) == buffer + 32);
    NX_CHECK(AlignUp(buffer + 32, 32) == buffer + 32);
}

NX_TEST(Allocation, NewDeleteAndUniquePtr) {
    PageAllocator pages;
    Heap heap(pages);
    TrackingAllocator tracked(heap, MemoryTag::Make(ModuleId::Memory, 1));

    Counted* const raw = New<Counted>(tracked, 5);
    NX_CHECK(raw->value == 5 && Counted::s_alive == 1);
    NX_CHECK(tracked.GetStatistics().liveAllocationCount == 1);
    Delete(tracked, raw);
    Delete<Counted>(tracked, nullptr); // Null is ignored.
    NX_CHECK(Counted::s_alive == 0 && tracked.GetStatistics().liveAllocationCount == 0);

    {
        UniquePtr<Counted> first = MakeUnique<Counted>(tracked, 7);
        NX_REQUIRE(static_cast<bool>(first));
        NX_CHECK(first->value == 7 && (*first).value == 7);
        NX_CHECK(first.GetAllocator() == &tracked);

        UniquePtr<Counted> second(Nexyl::Move(first));
        NX_CHECK(!first && second && Counted::s_alive == 1);

        first = MakeUnique<Counted>(tracked, 8);
        first = Nexyl::Move(second); // Destroys the object first held.
        NX_CHECK(Counted::s_alive == 1 && first->value == 7);

        first.Reset();
        NX_CHECK(!first && Counted::s_alive == 0);
        second = MakeUnique<Counted>(tracked, 9);
    }
    NX_CHECK(Counted::s_alive == 0);
    NX_CHECK(tracked.GetStatistics().liveAllocationCount == 0);
    NX_CHECK(tracked.GetStatistics().totalAllocationCount == 4);
}

NX_TEST(Allocation, AllocateAndReallocateKeepContents) {
    PageAllocator pages;
    Heap heap(pages);
    u8* const block = static_cast<u8*>(Allocate(heap, 64));
    for (u8 index = 0; index < 64; ++index) {
        block[index] = index;
    }
    u8* const grown = static_cast<u8*>(Reallocate(heap, block, 64, 4096));
    NX_CHECK(grown[0] == 0 && grown[63] == 63);
    heap.Deallocate(grown, 4096);
}

NX_TEST(Allocation, OutOfMemoryHandlerIsInstalledAndRestored) {
    // A failing allocation terminates the process, so only installing and restoring is tested here.
    const OutOfMemoryHandler previous = SetOutOfMemoryHandler(&IgnoreOutOfMemory);
    NX_CHECK(SetOutOfMemoryHandler(previous) == &IgnoreOutOfMemory);
}
