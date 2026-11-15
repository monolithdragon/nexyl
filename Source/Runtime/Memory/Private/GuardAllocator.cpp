#include <Nexyl/Memory/GuardAllocator.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Memory/Alignment.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Platform/SystemInfo.h>
#include <Nexyl/Platform/VirtualMemory.h>

// Layout of one reservation:
//
//   | guard page | (unused) | data pages, block at their end | guard page | rest of the granule |
//
// Only the data pages are committed. The block ends at the start of the trailing guard page, rounded down to its
// alignment. Release decommits the data pages and leaves the whole range reserved.

namespace Nexyl::Memory {

GuardAllocator::GuardAllocator() {
    const Platform::SystemInfo info = Platform::GetSystemInfo();
    m_pageSize = info.pageSize;
    m_allocationGranularity = info.allocationGranularity;
}

GuardAllocator::~GuardAllocator() {
    NX_ASSERT(m_counters.Get().liveAllocationCount == 0, "GuardAllocator destroyed while blocks from it are alive.");
}

Result<void*, Error> GuardAllocator::TryAllocate(usize size, usize alignment) {
    NX_ASSERT(size != 0, "Allocation size must not be zero.");
    NX_ASSERT(IsPowerOfTwo(alignment) && alignment <= m_pageSize,
              "GuardAllocator supports power-of-two alignments up to the page size.");
    if (size > kMaxAllocationSize) {
        return Error(MemoryError::OutOfMemory);
    }

    // Rounding the start down to the alignment can move it up to alignment - 1 bytes into an extra page.
    const usize dataBytes = AlignUp(size + alignment - 1, m_pageSize);
    const usize reservation = AlignUp(m_pageSize + dataBytes + m_pageSize, m_allocationGranularity);
    const Result<void*, Error> reserved = Platform::ReserveAddressSpace(reservation);
    if (reserved.IsError()) {
        return Error(MemoryError::OutOfMemory, reserved.GetError().GetDetail());
    }

    u8* const guard = static_cast<u8*>(reserved.GetValue()) + m_pageSize + dataBytes;
    // The block ends as close to the guard page as its alignment allows.
    u8* const start = guard - size - (ToAddress(guard - size) & (alignment - 1));
    u8* const firstPage = start - (ToAddress(start) & (m_pageSize - 1));
    const usize committed = static_cast<usize>(guard - firstPage);
    const Result<void, Error> commit = Platform::CommitMemory(firstPage, committed);
    if (commit.IsError()) {
        Platform::ReleaseAddressSpace(reserved.GetValue(), reservation);
        return Error(MemoryError::OutOfMemory, commit.GetError().GetDetail());
    }

    m_counters.OnAllocate(size);
    m_counters.AddReserved(committed);
    return static_cast<void*>(start);
}

Result<void*, Error> GuardAllocator::TryReallocate(void* block, usize oldSize, usize newSize, usize alignment) {
    NX_ASSERT(block != nullptr && newSize != 0, "Reallocate needs a block and a non-zero size.");
    NX_TRY_ASSIGN(void* const moved, TryAllocate(newSize, alignment));
    Copy(moved, block, oldSize < newSize ? oldSize : newSize);
    Deallocate(block, oldSize);
    return moved;
}

void GuardAllocator::Deallocate(void* block, usize size) {
    if (block == nullptr) {
        return;
    }
    // The committed range runs from the block's first page to the guard page that its end touches.
    const usize firstPage = AlignDown(ToAddress(block), m_pageSize);
    const usize guard = AlignUp(ToAddress(block) + size, m_pageSize);
    u8* const pages = static_cast<u8*>(block) - (ToAddress(block) - firstPage);
    [[maybe_unused]] const Result<void, Error> decommitted = Platform::DecommitMemory(pages, guard - firstPage);
    NX_ASSERT(decommitted.IsOk(), "Block does not come from this GuardAllocator.");
    m_counters.OnDeallocate(size);
    m_counters.RemoveReserved(guard - firstPage);
}

bool GuardAllocator::Owns(const void* block) const {
    return block != nullptr;
}

AllocatorStatistics GuardAllocator::GetStatistics() const {
    return m_counters.Get();
}

} // namespace Nexyl::Memory
